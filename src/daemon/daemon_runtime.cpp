#include "daemon.hpp"
#include "../config/config_writer.hpp"
#include "../util/safe_exec.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <set>
#include <sstream>

#include "../config/routing_state.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_runtime.hpp"
#include "../firewall/iptables.hpp"
#include "../log/logger.hpp"
#include "../routing/urltest_manager.hpp"

#include "../routing/routing_reconciler.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/cron.hpp"
#include "scheduler.hpp"

namespace keen_pbr3 {

namespace {
// How often the daemon verifies that dnsmasq still serves the keen-pbr config.
constexpr auto kDnsmasqCheckInterval = std::chrono::seconds{30};

class FailedApplyCounter {
public:
    explicit FailedApplyCounter(std::atomic<uint64_t>& counter) : counter_(counter) {}
    ~FailedApplyCounter() {
        if (!completed_) counter_.fetch_add(1, std::memory_order_relaxed);
    }
    void complete() { completed_ = true; }
private:
    std::atomic<uint64_t>& counter_;
    bool completed_{false};
};
} // namespace

// Maps a runtime lifecycle reason to the wording used when dnsmasq is
// restarted because of it.
std::string dnsmasq_apply_reason(std::string_view lifecycle_reason) {
    if (lifecycle_reason == "startup complete" || lifecycle_reason == "runtime started") {
        return "keen-pbr runtime started";
    }
    if (lifecycle_reason == "config apply verified" || lifecycle_reason == "config apply complete") {
        return "config applied";
    }
    if (lifecycle_reason == "configuration rollback verified") {
        return "rollback";
    }
    if (lifecycle_reason == "runtime restarted" ||
        lifecycle_reason == "lifecycle operation complete") {
        return "runtime restarted";
    }
    return std::string(lifecycle_reason);
}

bool Daemon::routing_runtime_active() const {
    return runtime_state_store_.snapshot().routing_runtime_active;
}

void Daemon::transition_runtime_or_throw(RuntimeState next, const char* reason) {
    std::string error;
    if (!runtime_state_machine_.transition(next, reason, error)) {
        throw DaemonError(error);
    }
}

void Daemon::stop_routing_runtime() {
    teardown_routing_and_firewall(true);
    Logger::instance().info("Routing runtime stopped.");
}

void Daemon::teardown_routing_and_firewall(bool explicit_stop) {
    auto& log = Logger::instance();
    if (!routing_runtime_active_) {
        return;
    }

    stop_dnsmasq_check();
    host_table_cache_.invalidate();
    invalidate_routing_health_cache();
    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);
    cancel_startup_list_retry();

    if (interface_refresh_task_id_ >= 0) {
        scheduler_->cancel(interface_refresh_task_id_);
        interface_refresh_task_id_ = -1;
    }
    interface_refresh_pending_ = false;
    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    pending_urltest_conntrack_cleanup_.clear();
    balance_classifier_cache_.clear();
    const uint32_t mark_mask = fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{}));
    std::set<uint32_t> owned_marks;
    for (const auto& [tag, mark] : outbound_marks_) {
        (void)tag;
        owned_marks.insert(mark);
    }
    for (uint32_t mark : owned_marks) {
        if (!conntrack_manager_.delete_mark(mark, mark_mask)) {
            log.warn("Best-effort conntrack cleanup failed for mark {:#x}/{:#x}",
                     mark, mark_mask);
        }
    }
    policy_rules_.clear();
    route_table_.clear();
    firewall_->cleanup();
    // The rules are gone; only now unbind so no queued packet is dropped.
    stop_intercept_service();
    firewall_state_.clear_active_firewall();

    routing_runtime_active_ = false;
    transition_runtime_or_throw(explicit_stop ? RuntimeState::stopped : RuntimeState::applying,
                                explicit_stop ? "runtime stopped" : "runtime restarting");
    publish_runtime_state();
    log.info("Routing and firewall stopped.");
}

void Daemon::start_routing_runtime() {
    setup_routing_and_firewall();
    complete_running_runtime("runtime started");
    Logger::instance().info("Routing runtime started.");
}

void Daemon::setup_routing_and_firewall() {
    if (routing_runtime_active_) {
        return;
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    const auto main_routes = netlink_.dump_routes_in_table(254);
    setup_static_routing(&main_routes);
    apply_firewall(FirewallApplyMode::Destructive, false, &main_routes);
    routing_runtime_active_ = true;
    if (runtime_state_machine_.state() != RuntimeState::applying) {
        transition_runtime_or_throw(RuntimeState::applying, "runtime starting");
    }
    publish_runtime_state();
    begin_runtime_generation();
}

void Daemon::complete_running_runtime(const char* reason, bool defer_dnsmasq_sync) {
    register_urltest_outbounds();
    schedule_lists_autoupdate();
    transition_runtime_or_throw(RuntimeState::running, reason);
    config_reload_last_success_s_.store(unix_timestamp_now_seconds(), std::memory_order_relaxed);
    publish_runtime_state();
    // Lifecycle operations are explicit user/boot actions: install the config
    // (not counted against the automatic repair budget, which they refill).
    // The sync also handles a switch to dns.resolver_integration=none.
    // When a persisted config apply defers sync, the caller writes config.json
    // first, then calls this explicitly after the write succeeds, ensuring
    // dnsmasq's conf-script reads the updated config from disk.
    if (!defer_dnsmasq_sync) {
        schedule_dnsmasq_sync(dnsmasq_apply_reason(reason), /*explicit_apply=*/true);
    }
    // While the DNS rules module is off dnsmasq is never probed: no check task.
    if (dnsmasq_integration_enabled(config_)) {
        start_dnsmasq_check();
    } else {
        stop_dnsmasq_check();
    }
}

void Daemon::start_dnsmasq_check() {
    stop_dnsmasq_check();
    dnsmasq_check_task_id_ = scheduler_->schedule_repeating(
        kDnsmasqCheckInterval,
        [this] {
            dnsmasq_manager_.request_check(
                [this](std::function<void()> task) {
                    return blocking_executor_.try_post("dnsmasq-check", std::move(task));
                });
        },
        "dnsmasq-check");
}

void Daemon::stop_dnsmasq_check() {
    if (dnsmasq_check_task_id_ >= 0) {
        scheduler_->cancel(dnsmasq_check_task_id_);
        dnsmasq_check_task_id_ = -1;
    }
}

void Daemon::schedule_dnsmasq_sync(std::string reason, bool explicit_apply) {
    dnsmasq_manager_.request_sync(
        config_, list_service_.cache_manager(),
        [this](std::function<void()> task) {
            return blocking_executor_.try_post("dnsmasq-sync", std::move(task));
        },
        explicit_apply, std::move(reason));
}

void Daemon::restart_routing_runtime() {
    if (!routing_runtime_active_) {
        throw DaemonError("Routing runtime is stopped");
    }

    teardown_routing_and_firewall(false);
    setup_routing_and_firewall();
    complete_running_runtime("runtime restarted");
}

void Daemon::setup_static_routing(const std::vector<DumpedRoute>* main_routes) {
    reconcile_static_routing(nullptr, main_routes);
}

void Daemon::reconcile_static_routing(
    const std::map<std::string, std::string>* urltest_selections,
    const std::vector<DumpedRoute>* main_routes) {
    const Ipv6SupportDecision ipv6_decision = resolve_ipv6_support(config_);
    log_ipv6_support_decision_once(ipv6_decision);
    const auto interfaces = netlink_.dump_interfaces();
    const auto owned_main_routes = main_routes != nullptr
        ? *main_routes
        : netlink_.dump_routes_in_table(254);
    RouteTable desired_routes(netlink_, true);
    PolicyRuleManager desired_rules(netlink_, true);
    populate_routing_state(
        config_,
        outbound_marks_,
        desired_routes,
        desired_rules,
        [&owned_main_routes](const Outbound& outbound) {
            return is_interface_outbound_reachable(outbound, owned_main_routes);
        },
        urltest_selections != nullptr
            ? urltest_selections
            : &firewall_state_.get_urltest_selections(),
        ipv6_decision.enabled,
        [&interfaces](const Outbound& outbound, int family) {
            if (family == AF_INET) return true;
            if (family != AF_INET6 || outbound.gateway6.has_value()) return true;
            const auto interface_name = outbound.interface.value_or("");
            const auto it = std::find_if(
                interfaces.begin(), interfaces.end(),
                [&interface_name](const DumpedInterface& interface) {
                    return interface.name == interface_name;
                });
            return it != interfaces.end() && interface_has_routed_ipv6(*it);
        },
        &owned_main_routes);

    // Inspect the kernel on every apply so a restarted daemon adopts intact
    // state and only removes objects with a verifiable ownership marker.
    RoutingReconciler(netlink_).reconcile(desired_routes.get_routes(),
                                          desired_rules.get_rules());
    route_table_.adopt_desired(desired_routes.get_routes());
    policy_rules_.adopt_desired(desired_rules.get_rules());
}

void Daemon::apply_firewall(FirewallApplyMode mode,
                            bool force_clear_dynamic_sets,
                            const std::vector<DumpedRoute>* main_routes,
                            const Config* quiesce_config,
                            const OutboundMarkMap* quiesce_marks) {
    host_table_cache_.invalidate();
    invalidate_routing_health_cache();
    // An apply (re)binds the listeners itself and supersedes a pending
    // automatic re-bind; the next tick starts over if one is still dead.
    if (!intercept_rebind_in_progress_) {
        intercept_rebind_.reset();
        publish_intercept_rebind_status();
    }
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        // Any in-flight build belongs to the previous apply attempt. It must
        // not publish after this attempt fails or changes the set schema.
        intercept_snapshot_seq_.fetch_add(1, std::memory_order_acq_rel);
    }
    const auto owned_main_routes = main_routes != nullptr
        ? *main_routes
        : netlink_.dump_routes_in_table(254);
    const auto interfaces = netlink_.dump_interfaces();
    const auto balance_candidates = build_balance_candidates(owned_main_routes, interfaces);
    std::shared_ptr<InterceptService> service_for_apply;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service_for_apply = intercept_service_;
    }
    // Prevent an already-published snapshot from writing while this apply
    // creates, replaces, or retargets sets with the same logical names. The
    // gates drain admitted writes without holding a mutex across the writer's
    // potentially slow netlink transaction, so stopping a service cannot
    // deadlock behind the pause.
    std::optional<InterceptService::WritePause> writer_pause;
    if (service_for_apply) {
        writer_pause.emplace(service_for_apply->pause_writes());
        service_for_apply->discard_l7_pending();
        // The published snapshot stays: its domain index is still what the
        // lists describe, and nothing can write while the pause lasts.  The
        // element cache must not outlive the sets, though; with writes paused
        // nothing can repopulate it before the rebind below clears it again.
        service_for_apply->invalidate_set_cache();
    }
    // The old snapshot's set bindings are only valid until this apply changes
    // the sets.  Unless the new bindings are published before the pause ends
    // (schedule_intercept_snapshot_update below), drop the snapshot so a failed
    // or abandoned apply can never resume writes against recreated sets.
    // Declared after writer_pause: it runs while the pause is still held.
    struct SnapshotRebindGuard {
        std::shared_ptr<InterceptService> service;
        bool rebound{false};
        ~SnapshotRebindGuard() {
            if (service && !rebound) service->invalidate_snapshot();
        }
    } rebind_guard{service_for_apply};

    // Interception ordering: a service that must go away or rebind is
    // detached from the rules first (quiesce), a needed service is bound
    // before any rule queues to it.
    InterceptEffective intercept = resolve_intercept_effective();
    {
        const InterceptServiceOptions wanted = [&intercept] {
            InterceptServiceOptions options;
            if (intercept.dns_hold) options.queue_num = intercept.queue_num;
            if (intercept.l7) options.nflog_group = intercept.nflog_group;
            options.hold_timeout_ms = intercept.hold_timeout_ms;
            options.conntrack_cleanup = intercept.conntrack_cleanup;
            return options;
        }();
        bool service_present;
        bool service_running;
        bool listener_unbound;
        {
            KPBR_LOCK_GUARD(intercept_mutex_);
            service_present = intercept_service_ != nullptr;
            service_running = service_present && intercept_service_->running();
            // A listener that failed to bind at start or died at runtime (NFLOG
            // failure leaves the hot loop alive for DNS, so running() alone
            // cannot trigger recovery).  Only parts that are still wanted
            // count: a part disabled by a probe verdict is not retried here.
            listener_unbound =
                service_present &&
                ((intercept.dns_hold && !intercept_service_->dns_bound()) ||
                 (intercept.l7 && !intercept_service_->l7_bound()));
        }
        const bool options_changed =
            wanted.queue_num != intercept_service_options_.queue_num ||
            wanted.nflog_group != intercept_service_options_.nflog_group ||
            wanted.hold_timeout_ms != intercept_service_options_.hold_timeout_ms ||
            wanted.conntrack_cleanup != intercept_service_options_.conntrack_cleanup;
        // An explicit runtime apply must rebind the desired listener while
        // preserving the normal quiesce/rollback ordering and degraded health
        // until the replacement is ready.
        if (service_present &&
            (!service_running || !intercept.active() || options_changed || listener_unbound)) {
            quiesce_intercept_service(
                owned_main_routes, interfaces, balance_candidates,
                quiesce_config != nullptr ? *quiesce_config : config_,
                quiesce_marks != nullptr ? *quiesce_marks : outbound_marks_);
        }
        if (intercept.active()) {
            std::shared_ptr<InterceptService> kept_service;
            {
                KPBR_LOCK_GUARD(intercept_mutex_);
                kept_service = intercept_service_;
            }
            if (!kept_service) {
                start_intercept_service(intercept);
            } else {
                // The capability re-probe cannot measure listeners (that
                // takes a bind); the running service's own results are the
                // authoritative ones, carry_forward is only the fallback.
                fold_intercept_listener_probe(intercept, *kept_service);
            }
            if (intercept.l7 && firewall_->backend() == FirewallBackend::nftables) {
                (void)enable_conntrack_accounting();
            }
        }
        {
            KPBR_LOCK_GUARD(intercept_mutex_);
            intercept_effective_ = intercept;
        }
    }

    const auto previous_active = firewall_state_.active_firewall();
    FailedApplyCounter failed_apply(firewall_apply_errors_);
    auto active = apply_runtime_firewall(
        config_,
        outbound_marks_,
        list_service_.cache_manager(),
        *firewall_,
        mode,
        previous_active.get(),
        force_clear_dynamic_sets,
        owned_main_routes,
        interfaces,
        &balance_candidates,
        intercept.firewall_settings());
    failed_apply.complete();
    // The apply may have recreated or flushed the dynamic sets: whatever the
    // interception cache remembered about their elements is no longer true.
    // (The cache was cleared when the pause began; this also covers a service
    // that was started during the apply.)
    {
        std::shared_ptr<InterceptService> service_after_apply;
        {
            KPBR_LOCK_GUARD(intercept_mutex_);
            service_after_apply = intercept_service_;
        }
        if (service_after_apply) service_after_apply->invalidate_set_cache();
    }
    // Derived from the plan that was actually applied, never recomputed.
    const ConntrackPolicy conntrack_policy = conntrack_policy_for_plan(active.plan);
    auto applied_sets = active.plan.sets;
    firewall_state_.publish_active_firewall(std::move(active));
    (void)conntrack_manager_.reconcile(conntrack_policy);
    // Only after the apply that created the sets succeeded.  Whether the
    // daemon can write the dynamic sets (set write, nft timeout update) is a
    // kernel capability, measured once at service start on a scratch set; the
    // writers report runtime write errors and health shows them, so the apply
    // does not test the freshly created sets again.
    if (intercept.active()) {
        schedule_intercept_snapshot_update(std::move(applied_sets), intercept);
        rebind_guard.rebound = true;
    }
}

FirewallBalanceCandidates Daemon::build_balance_candidates(
    const std::vector<DumpedRoute>& main_routes,
    const std::vector<DumpedInterface>& interfaces) {
    FirewallBalanceCandidates candidates;
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    (void)main_routes;
    (void)interfaces;
    return candidates;  // balancing is not compiled on Keenetic
#else
    if (!urltest_manager_) {
        return candidates;
    }

    const auto& outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
    const auto find_outbound = [&outbounds](const std::string& tag) -> const Outbound* {
        const auto it = std::find_if(outbounds.begin(), outbounds.end(),
                                     [&tag](const Outbound& outbound) {
                                         return outbound.tag == tag;
                                     });
        return it == outbounds.end() ? nullptr : &*it;
    };

    for (const auto& group : outbounds) {
        if (!outbound_uses_balance(group) ||
            (group.type != OutboundType::URLTEST && group.type != OutboundType::ICMPTEST)) {
            continue;
        }
        const auto state = urltest_manager_->get_state(group.tag);
        if (!state.has_value()) {
            continue;
        }
        for (const auto& tag : select_test_group_usable_outbounds(*state)) {
            const Outbound* child = find_outbound(tag);
            const auto mark = outbound_marks_.find(tag);
            if (!child || mark == outbound_marks_.end()) {
                continue;
            }

            FirewallBalanceCandidate candidate{mark->second};
            for (const auto& member_group : group.outbound_groups.value_or(
                     std::vector<OutboundGroup>{})) {
                const auto members = outbound_group_tags(member_group);
                if (std::find(members.begin(), members.end(), tag) != members.end()) {
                    candidate.weight = outbound_group_balance_weight(member_group, tag);
                    break;
                }
            }
            if (child->type == OutboundType::INTERFACE) {
                const bool family4 = is_interface_outbound_family_reachable(
                    *child, AF_INET, main_routes);
                const bool family6 = is_interface_outbound_family_reachable(
                    *child, AF_INET6, main_routes);
                candidate.ipv4 = family4 &&
                    (child->gateway.has_value() ||
                     (!child->gateway.has_value() && !child->gateway6.has_value()));
                candidate.ipv6 = family6 && child->gateway6.has_value();
                if (!candidate.ipv6 && !child->gateway.has_value()) {
                    const auto interface_name = child->interface.value_or("");
                    const auto interface = std::find_if(
                        interfaces.begin(), interfaces.end(),
                        [&interface_name](const DumpedInterface& value) {
                            return value.name == interface_name;
                        });
                    candidate.ipv6 = family6 && interface != interfaces.end() &&
                        interface_has_routed_ipv6(*interface);
                }
            }
            if (candidate.ipv4 || candidate.ipv6) {
                candidates[group.tag].push_back(candidate);
            }
        }
    }
    return candidates;
#endif
}

void Daemon::reconcile_lists_only() {
    if (!routing_runtime_active_) {
        throw DaemonError("list-only reconcile requires an active routing runtime");
    }

    try {
        apply_firewall(FirewallApplyMode::StaticSetsOnly);
        publish_runtime_state();
    } catch (...) {
        std::string ignored_error;
        (void)runtime_state_machine_.transition(RuntimeState::broken,
                                                "list-only reconcile failed",
                                                ignored_error);
        publish_runtime_state();
        throw;
    }
}

void Daemon::handle_urltest_selection_change(const std::string& urltest_tag,
                                             const std::string& new_child_tag) {
    const auto runtime_generation = runtime_generation_.load(std::memory_order_acquire);
    post_control_task([this, urltest_tag, new_child_tag, runtime_generation]() {
        auto& log = Logger::instance();
        if (runtime_generation != runtime_generation_.load(std::memory_order_acquire)) {
            log.trace("urltest_selection_skip",
                      "tag={} reason=stale_runtime_generation",
                      urltest_tag);
            return;
        }

        const auto applied_selections = firewall_state_.get_urltest_selections();
        const auto applied_it = applied_selections.find(urltest_tag);
        const std::string old_child_tag = applied_it == applied_selections.end()
            ? std::string{}
            : applied_it->second;

        const auto configured_outbounds =
            config_.outbounds.value_or(std::vector<Outbound>{});
        const auto configured = std::find_if(
            configured_outbounds.begin(), configured_outbounds.end(),
            [&urltest_tag](const Outbound& outbound) { return outbound.tag == urltest_tag; });
#ifndef KEEN_PBR_PLATFORM_KEENETIC
        const bool balance = configured != configured_outbounds.end() &&
            outbound_uses_balance(*configured);
        if (balance) {
            std::set<uint32_t> cleanup_marks;
            const auto state = urltest_manager_
                ? urltest_manager_->get_state(urltest_tag)
                : std::optional<UrltestState>{};
            const auto child_failed = [&state](const std::string& child_tag) {
                if (!state.has_value()) return false;
                const auto breaker = state->circuit_breakers.find(child_tag);
                if (breaker != state->circuit_breakers.end() &&
                    breaker->second.state(child_tag) == CircuitState::open) {
                    return true;
                }
                const auto result = state->last_results.find(child_tag);
                return result != state->last_results.end() && !result->second.success;
            };
            // Probe results arrive every cycle; rebuild only when the inputs
            // of the classifier change, and flush a failed child's
            // connections once, when it fails. For balance groups, only
            // failed children's connections are flushed, regardless of
            // conntrack_on_switch (unlike priority groups).
            const auto owned_main_routes = netlink_.dump_routes_in_table(254);
            const auto interfaces = netlink_.dump_interfaces();
            const auto all_candidates = build_balance_candidates(owned_main_routes, interfaces);
            BalanceClassifierState classifier;
            classifier.selected_child = new_child_tag;
            if (const auto it = all_candidates.find(urltest_tag); it != all_candidates.end()) {
                classifier.candidates = it->second;
            }
            for (const auto& group : configured->outbound_groups.value_or(
                     std::vector<OutboundGroup>{})) {
                for (const auto& child_tag : outbound_group_tags(group)) {
                    if (child_failed(child_tag)) classifier.failed_children.insert(child_tag);
                }
            }
            const auto applied_it = balance_classifier_cache_.find(urltest_tag);
            const BalanceClassifierState* applied =
                applied_it == balance_classifier_cache_.end() ? nullptr : &applied_it->second;
            const auto newly_failed = newly_failed_children(applied, classifier.failed_children);
            // Usable members of the OTHER balance groups. Only balance groups
            // matter: priority groups save their own group mark in the
            // connmark, never a member's mark, so their selection pins no
            // flow to a member mark.
            std::map<std::string, std::vector<std::string>> usable_by_balance_group;
            if (!newly_failed.empty() && urltest_manager_) {
                for (const auto& other : configured_outbounds) {
                    if (other.tag == urltest_tag || !outbound_uses_balance(other) ||
                        (other.type != OutboundType::URLTEST &&
                         other.type != OutboundType::ICMPTEST)) {
                        continue;
                    }
                    const auto other_state = urltest_manager_->get_state(other.tag);
                    if (other_state.has_value()) {
                        usable_by_balance_group[other.tag] =
                            select_test_group_usable_outbounds(*other_state);
                    }
                }
            }
            for (const auto& group : configured->outbound_groups.value_or(
                     std::vector<OutboundGroup>{})) {
                for (const auto& child_tag : outbound_group_tags(group)) {
                    const auto mark = outbound_marks_.find(child_tag);
                    if (mark == outbound_marks_.end()) continue;
                    if (newly_failed.count(child_tag) != 0) {
                        // Health is per group but the mark (and so this flush)
                        // is per outbound. If another balance group still
                        // balances onto the member, keep its flows: the group
                        // that drops it last flushes on its own failure edge.
                        // Trade-off: flows this group pinned to the member
                        // stay there until they end; doing better needs
                        // group-aware connmarks.
                        if (member_usable_in_other_balance_group(
                                child_tag, urltest_tag, usable_by_balance_group)) {
                            continue;
                        }
                        cleanup_marks.insert(mark->second);
                    }
                }
            }
            if (applied != nullptr && *applied == classifier && cleanup_marks.empty()) {
                log.trace("urltest_selection_skip", "tag={} reason=unchanged", urltest_tag);
                return;
            }

            auto proposed_selections = applied_selections;
            proposed_selections[urltest_tag] = new_child_tag;
            try {
                // The group mark remains a scalar priority-selected path for
                // internal DNS/list detours. User route rules are rebuilt as
                // child-mark balancing classifiers below.
                reconcile_static_routing(&proposed_selections);
                apply_firewall(runtime_refresh_firewall_mode());
                firewall_state_.set_urltest_selection(urltest_tag, new_child_tag);
                const uint32_t mask = fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{}));
                for (const uint32_t mark : cleanup_marks) {
                    try {
                        if (!conntrack_manager_.delete_mark(mark, mask)) {
                            log.warn("Conntrack cleanup failed for failed balance child mark {}",
                                     mark);
                        }
                    } catch (const std::exception& e) {
                        log.warn("Conntrack cleanup failed for balance child mark {}: {}", mark,
                                 e.what());
                    } catch (...) {
                        log.warn("Conntrack cleanup failed for balance child mark {}: unknown error",
                                 mark);
                    }
                }
                publish_runtime_state(StatusPublishScope::Outbounds);

                balance_classifier_cache_[urltest_tag] = std::move(classifier);
                log.info("Updated balance classifier for test-group '{}' ({} child marks cleaned)",
                         urltest_tag, cleanup_marks.size());
            } catch (const std::exception& e) {
                try {
                    reconcile_static_routing(&applied_selections);
                } catch (const std::exception& rollback_error) {
                    log.error("Test-group '{}' balance update rollback failed: {}", urltest_tag,
                              rollback_error.what());
                }
                log.error("Test-group '{}' balance classifier update failed: {}", urltest_tag,
                          e.what());
            } catch (...) {
                try {
                    reconcile_static_routing(&applied_selections);
                } catch (...) {
                    log.error("Test-group '{}' balance update rollback failed", urltest_tag);
                }
                log.error("Test-group '{}' balance classifier update failed: unknown error",
                          urltest_tag);
            }
            return;
        }
#endif

        // The route is already applied when cleanup is pending.  A later
        // unchanged probe must retry only the targeted conntrack deletion;
        // reconciling the same selection again would needlessly touch routes
        // and could turn a transient cleanup failure into a routing failure.
        const auto pending_it = pending_urltest_conntrack_cleanup_.find(urltest_tag);
        if (old_child_tag == new_child_tag) {
            if (pending_it == pending_urltest_conntrack_cleanup_.end()) {
                return;
            }
            if (pending_it->second.selected_child != new_child_tag) {
                pending_urltest_conntrack_cleanup_.erase(pending_it);
                return;
            }

            const auto pending = pending_it->second;
            try {
                if (conntrack_manager_.delete_mark(pending.mark, pending.mark_mask)) {
                    pending_urltest_conntrack_cleanup_.erase(urltest_tag);
                    log.info("Conntrack cleanup retry completed after test-group '{}' switch",
                             urltest_tag);
                } else {
                    log.warn("Conntrack cleanup retry failed after test-group '{}' switch",
                             urltest_tag);
                }
            } catch (const std::exception& e) {
                log.warn("Conntrack cleanup retry failed after test-group '{}' switch: {}",
                         urltest_tag, e.what());
            } catch (...) {
                log.warn("Conntrack cleanup retry failed after test-group '{}' switch: unknown error",
                         urltest_tag);
            }
            return;
        }

        // A newer selection supersedes a failed cleanup for the old child.
        // Cleanup is mark-wide, so the new switch will either complete it or
        // install a fresh pending entry for the currently applied child.
        if (pending_it != pending_urltest_conntrack_cleanup_.end()) {
            pending_urltest_conntrack_cleanup_.erase(pending_it);
        }

        bool delete_on_healthy_switch = false;
        for (const auto& outbound : config_.outbounds.value_or(std::vector<Outbound>{})) {
            if (outbound.tag == urltest_tag &&
                outbound.conntrack_on_switch.value_or(api::ConntrackOnSwitch::PRESERVE) ==
                    api::ConntrackOnSwitch::DELETE) {
                delete_on_healthy_switch = true;
                break;
            }
        }

        bool old_child_healthy = false;
        if (!old_child_tag.empty() && urltest_manager_) {
            const auto state = urltest_manager_->get_state(urltest_tag);
            if (state.has_value()) {
                const auto result = state->last_results.find(old_child_tag);
                const auto breaker = state->circuit_breakers.find(old_child_tag);
                const bool breaker_open = breaker != state->circuit_breakers.end() &&
                    breaker->second.state(old_child_tag) == CircuitState::open;
                old_child_healthy = !breaker_open &&
                    result != state->last_results.end() && result->second.success;
            }
        }

        const auto switch_reason = classify_test_group_switch(
            !old_child_tag.empty(), old_child_healthy);
        const bool delete_conntrack = should_delete_test_group_conntrack(
            switch_reason, delete_on_healthy_switch);

        auto proposed_selections = applied_selections;
        proposed_selections[urltest_tag] = new_child_tag;
        log.info("Test group '{}' switch old='{}' new='{}' reason={} conntrack={}",
                 urltest_tag,
                 old_child_tag.empty() ? "(none)" : old_child_tag,
                 new_child_tag.empty() ? "(none)" : new_child_tag,
                 test_group_switch_reason_name(switch_reason),
                 delete_conntrack ? "delete" : "preserve");
        try {
            reconcile_static_routing(&proposed_selections);
            firewall_state_.set_urltest_selection(urltest_tag, new_child_tag);
            if (delete_conntrack) {
                const auto mark_it = outbound_marks_.find(urltest_tag);
                const uint32_t mark_mask = fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{}));
                bool cleanup_succeeded = false;
                if (mark_it != outbound_marks_.end()) {
                    try {
                        cleanup_succeeded = conntrack_manager_.delete_mark(
                            mark_it->second, mark_mask);
                    } catch (const std::exception& e) {
                        log.warn("Best-effort conntrack cleanup failed after test-group '{}' switch: {}",
                                 urltest_tag, e.what());
                    } catch (...) {
                        log.warn("Best-effort conntrack cleanup failed after test-group '{}' switch: unknown error",
                                 urltest_tag);
                    }
                }
                if (!cleanup_succeeded && mark_it != outbound_marks_.end()) {
                    pending_urltest_conntrack_cleanup_[urltest_tag] =
                        PendingUrltestConntrackCleanup{
                            new_child_tag, mark_it->second, mark_mask};
                    log.warn("Best-effort conntrack cleanup failed after test-group '{}' switch",
                             urltest_tag);
                } else if (cleanup_succeeded) {
                    pending_urltest_conntrack_cleanup_.erase(urltest_tag);
                    log.info("Conntrack cleanup completed after test-group '{}' switch",
                             urltest_tag);
                }
            }
            publish_runtime_state(StatusPublishScope::Outbounds);
            log.info("Routing policy updated after test-group '{}' switch", urltest_tag);
        } catch (const std::exception& e) {
            try {
                // Routing reconciliation is not atomic at the kernel level:
                // it may have added or removed some objects before reporting
                // an error. Restore the old desired state before exposing any
                // failure to the next probe sweep.
                reconcile_static_routing(&applied_selections);
                log.error("Test-group '{}' routing switch failed; restored applied selection '{}': {}",
                          urltest_tag,
                          old_child_tag.empty() ? "(none)" : old_child_tag,
                          e.what());
            } catch (const std::exception& rollback_error) {
                log.error("Test-group '{}' routing switch failed and rollback to applied selection '{}' failed: {}; rollback error: {}",
                          urltest_tag,
                          old_child_tag.empty() ? "(none)" : old_child_tag,
                          e.what(), rollback_error.what());
                std::string ignored_error;
                (void)runtime_state_machine_.transition(
                    RuntimeState::broken, "urltest routing rollback failed", ignored_error);
                publish_runtime_state();
            } catch (...) {
                log.error("Test-group '{}' routing switch failed and rollback to applied selection '{}' failed: {}; rollback error: unknown",
                          urltest_tag,
                          old_child_tag.empty() ? "(none)" : old_child_tag,
                          e.what());
                std::string ignored_error;
                (void)runtime_state_machine_.transition(
                    RuntimeState::broken, "urltest routing rollback failed", ignored_error);
                publish_runtime_state();
            }
        } catch (...) {
            try {
                reconcile_static_routing(&applied_selections);
                log.error("Test-group '{}' routing switch failed; restored applied selection '{}'",
                          urltest_tag,
                          old_child_tag.empty() ? "(none)" : old_child_tag);
            } catch (...) {
                log.error("Test-group '{}' routing switch failed and rollback to applied selection '{}' failed: unknown",
                          urltest_tag,
                          old_child_tag.empty() ? "(none)" : old_child_tag);
                std::string ignored_error;
                (void)runtime_state_machine_.transition(
                    RuntimeState::broken, "urltest routing rollback failed", ignored_error);
                publish_runtime_state();
            }
        }
    }, "urltest-selection-change:" + urltest_tag);
}

bool Daemon::commit_urltest_probe_results(const std::string& urltest_tag,
                                          std::uint64_t probe_generation,
                                          std::map<std::string, URLTestResult> results,
                                          TraceId trace_id) {
    return post_control_task(
        [this,
         urltest_tag,
         probe_generation,
         results = std::move(results),
         trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            if (!urltest_manager_) {
                Logger::instance().trace("urltest_commit_skip",
                                         "tag={} generation={} reason=missing_manager",
                                         urltest_tag,
                                         probe_generation);
                return;
            }
            urltest_manager_->commit_probe_results(urltest_tag,
                                                   probe_generation,
                                                   std::move(results));
            publish_urltest_runtime_state(urltest_tag);
        },
        "urltest-commit:" + urltest_tag);
}

void Daemon::register_urltest_outbounds() {
    if (!urltest_manager_) {
        urltest_manager_ = std::make_unique<UrltestManager>(
            url_tester_,
            icmp_tester_,
            outbound_marks_,
            *scheduler_,
            blocking_executor_,
            [this](const std::string& urltest_tag, const std::string& new_child_tag) {
                handle_urltest_selection_change(urltest_tag, new_child_tag);
            },
            [this](const std::string& urltest_tag,
                   std::uint64_t probe_generation,
                   std::map<std::string, URLTestResult> results,
                   TraceId trace_id) mutable {
                Logger::instance().trace("urltest_commit_enqueue",
                                         "tag={} generation={}",
                                         urltest_tag,
                                         probe_generation);
                return commit_urltest_probe_results(urltest_tag,
                                                    probe_generation,
                                                    std::move(results),
                                                    trace_id);
            });
    }

    for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
        if (ob.type == OutboundType::URLTEST || ob.type == OutboundType::ICMPTEST) {
            urltest_manager_->register_urltest(ob);
        }
    }
}

void Daemon::schedule_lists_autoupdate() {
    // Idempotent: never leave an earlier timer behind when rescheduling.
    if (lists_autoupdate_task_id_ >= 0) {
        scheduler_->cancel(lists_autoupdate_task_id_);
        lists_autoupdate_task_id_ = -1;
    }
    if (!config_.lists_autoupdate) return;
    if (!config_.lists_autoupdate->enabled.value_or(false)) return;
    const auto& expr = config_.lists_autoupdate->cron.value_or("");
    auto next = cron_next(expr);
    const auto now = std::chrono::system_clock::now();
    auto delay = std::chrono::ceil<std::chrono::seconds>(next - now);
    if (delay.count() < 1) delay = std::chrono::seconds{1};
    lists_autoupdate_task_id_ = scheduler_->schedule_oneshot(
        delay,
        [this]() {
            refresh_lists_and_maybe_reload_async();
        },
        "lists-autoupdate");
    Logger::instance().info("Lists autoupdate scheduled (next: ~{}s)", delay.count());
}

ListsRefreshExecutionResult Daemon::apply_list_refresh_result(
    RemoteListsRefreshResult refresh_result,
    bool runtime_active,
    std::string_view source) {
    auto& log = Logger::instance();
    ListsRefreshExecutionResult result;
    result.refresh_result = std::move(refresh_result);

    if (result.refresh_result.any_changed()) {
        // Domain lists feed the generated dnsmasq config; the content hash
        // makes this a no-op when no dns.rules list was affected.
        schedule_dnsmasq_sync();
    }

    if (!result.refresh_result.changed_lists.empty()) {
        log.info("Lists refresh ({}): updated list(s): {}", source,
                 format_list_names(result.refresh_result.changed_lists));
    } else if (!result.refresh_result.failed_lists.empty()) {
        log.warn("Lists refresh ({}): failed list(s): {}", source,
                 format_list_names(result.refresh_result.failed_lists));
    } else {
        log.info("Lists refresh ({}): all checked list(s) are up-to-date.", source);
    }

    if (should_reload_runtime_after_list_refresh(runtime_active, result.refresh_result)) {
        log.info("Lists refresh ({}): relevant list(s) changed ({}), reloading runtime",
                 source,
                 format_list_names(result.refresh_result.relevant_changed_lists));
        reconcile_lists_only();
        result.reloaded = true;
        return result;
    }

    if (result.refresh_result.any_relevant_changed()) {
        log.info("Lists refresh: relevant list(s) changed ({}), but runtime is stopped",
                 format_list_names(result.refresh_result.relevant_changed_lists));
    } else if (result.refresh_result.any_changed()) {
        log.info("Lists refresh: updated list(s) did not affect runtime config: {}",
                 format_list_names(result.refresh_result.changed_lists));
    } else if (result.refresh_result.any_failed()) {
        log.warn("Lists refresh: failed to refresh list(s): {}",
                 format_list_names(result.refresh_result.failed_lists));
    } else {
        log.info("Lists refresh: no list updates");
    }

    return result;
}

ListsRefreshExecutionResult Daemon::execute_remote_list_refresh(
    const std::set<std::string>* target_lists,
    std::string_view source) {
    const auto relevant_lists = collect_relevant_list_names(config_);
    auto refresh_result =
        list_service_.refresh_remote_lists(
            config_, outbound_marks_, &relevant_lists, target_lists);
    return apply_list_refresh_result(
        std::move(refresh_result), routing_runtime_active_, source);
}

void Daemon::refresh_lists_and_maybe_reload() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    try {
        execute_remote_list_refresh(nullptr, "autoupdate");
        // The timer is a one-shot that has already fired, and a list-only
        // reconcile does not reschedule it: always schedule the next run here,
        // reloaded or not.
        schedule_lists_autoupdate();
    } catch (const std::exception& e) {
        log.error("Lists autoupdate failed: {}", e.what());
        schedule_lists_autoupdate();
    }
}

void Daemon::commit_lists_refresh_async_result(
    Config config_snapshot,
    bool runtime_active_snapshot,
    std::uint64_t generation,
    std::optional<RemoteListsRefreshResult> refresh_result,
    std::string error,
    TraceId trace_id) {
    post_control_task(
        [this,
         config_snapshot = std::move(config_snapshot),
         runtime_active_snapshot,
         generation,
         refresh_result = std::move(refresh_result),
         error = std::move(error),
         trace_id]() mutable {
            ScopedTraceContext trace_scope_inner(trace_id);
            remote_list_refresh_inflight_.store(false, std::memory_order_release);

            if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                Logger::instance().trace("lists_refresh_skip",
                                         "source=autoupdate generation={} reason=stale_runtime",
                                         generation);
                schedule_lists_autoupdate();
                return;
            }

            if (!error.empty()) {
                Logger::instance().error("Lists autoupdate failed: {}", error);
                schedule_lists_autoupdate();
                return;
            }

            try {
                apply_list_refresh_result(
                    std::move(*refresh_result), runtime_active_snapshot, "autoupdate");
            } catch (const std::exception& e) {
                Logger::instance().error("Lists autoupdate reload failed: {}", e.what());
            }
            schedule_lists_autoupdate();
        },
        "lists-refresh-commit");
}

void Daemon::refresh_lists_and_maybe_reload_async() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    bool expected = false;
    if (!remote_list_refresh_inflight_.compare_exchange_strong(expected,
                                                               true,
                                                               std::memory_order_acq_rel)) {
        Logger::instance().trace("lists_refresh_skip",
                                 "source=autoupdate reason=inflight");
        return;
    }

    const Config config_snapshot = config_;
    const OutboundMarkMap marks_snapshot = outbound_marks_;
    const bool runtime_active_snapshot = routing_runtime_active_;
    const auto relevant_lists = collect_relevant_list_names(config_snapshot);
    const auto generation = runtime_generation_.load(std::memory_order_acquire);
    const TraceId trace_id = ensure_trace_id();

    const bool enqueued = blocking_executor_.try_post(
        "lists-autoupdate",
        [this,
         config_snapshot,
         marks_snapshot,
         runtime_active_snapshot,
         relevant_lists,
         generation,
         trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            std::optional<RemoteListsRefreshResult> refresh_result;
            std::string error;

            Logger::instance().trace("lists_refresh_start",
                                     "source=autoupdate generation={}",
                                     generation);
            try {
                refresh_result = list_service_.refresh_remote_lists(config_snapshot,
                                                                   marks_snapshot,
                                                                   &relevant_lists,
                                                                   nullptr);
            } catch (const std::exception& e) {
                error = e.what();
            }

            commit_lists_refresh_async_result(config_snapshot,
                                              runtime_active_snapshot,
                                              generation,
                                              std::move(refresh_result),
                                              std::move(error),
                                              trace_id);
        },
        trace_id);

    if (!enqueued) {
        remote_list_refresh_inflight_.store(false, std::memory_order_release);
        Logger::instance().trace("lists_refresh_skip",
                                 "source=autoupdate reason=executor_unavailable");
        schedule_lists_autoupdate();
    }
}

void Daemon::cancel_startup_list_retry() {
    if (startup_list_retry_task_id_ >= 0) {
        scheduler_->cancel(startup_list_retry_task_id_);
        startup_list_retry_task_id_ = -1;
    }
}

void Daemon::schedule_startup_list_retry() {
    cancel_startup_list_retry();
    const auto delay = startup_list_retry_delay(startup_list_retry_attempt_);
    startup_list_retry_task_id_ = scheduler_->schedule_oneshot(
        delay,
        [this]() {
            startup_list_retry_task_id_ = -1;
            run_startup_list_retry();
        },
        "startup-list-retry");
    Logger::instance().info(
        "Startup lists: retrying failed download(s) in {}s (attempt {})",
        delay.count(), startup_list_retry_attempt_ + 1);
}

void Daemon::run_startup_list_retry() {
    if (!routing_runtime_active_) {
        return;
    }

    bool expected = false;
    if (!remote_list_refresh_inflight_.compare_exchange_strong(expected,
                                                               true,
                                                               std::memory_order_acq_rel)) {
        Logger::instance().trace("lists_refresh_skip",
                                 "source=startup-retry reason=inflight");
        schedule_startup_list_retry();
        return;
    }

    const Config config_snapshot = config_;
    const OutboundMarkMap marks_snapshot = outbound_marks_;
    const auto relevant_lists = collect_relevant_list_names(config_snapshot);
    const auto generation = runtime_generation_.load(std::memory_order_acquire);
    const TraceId trace_id = ensure_trace_id();

    const bool enqueued = blocking_executor_.try_post(
        "startup-list-retry",
        [this, config_snapshot, marks_snapshot, relevant_lists, generation, trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            std::optional<RemoteListsRefreshResult> refresh_result;
            std::string error;
            try {
                // Cached lists are skipped, so only the failed ones are retried.
                refresh_result = list_service_.download_uncached(
                    config_snapshot, marks_snapshot, &relevant_lists);
            } catch (const std::exception& e) {
                error = e.what();
            } catch (...) {
                error = "unknown list download error";
            }
            commit_startup_list_retry_result(
                generation, std::move(refresh_result), std::move(error), trace_id);
        },
        trace_id);

    if (!enqueued) {
        remote_list_refresh_inflight_.store(false, std::memory_order_release);
        Logger::instance().trace("lists_refresh_skip",
                                 "source=startup-retry reason=executor_unavailable");
        schedule_startup_list_retry();
    }
}

void Daemon::commit_startup_list_retry_result(
    std::uint64_t generation,
    std::optional<RemoteListsRefreshResult> refresh_result,
    std::string error,
    TraceId trace_id) {
    post_control_task(
        [this,
         generation,
         refresh_result = std::move(refresh_result),
         error = std::move(error),
         trace_id]() mutable {
            ScopedTraceContext trace_scope_inner(trace_id);
            remote_list_refresh_inflight_.store(false, std::memory_order_release);

            if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                Logger::instance().trace("lists_refresh_skip",
                                         "source=startup-retry generation={} reason=stale_runtime",
                                         generation);
                return;
            }

            bool failures_remain = true;
            if (!error.empty() || !refresh_result.has_value()) {
                Logger::instance().error("Startup lists retry failed: {}", error);
            } else {
                failures_remain = refresh_result->any_failed();
                try {
                    apply_list_refresh_result(
                        std::move(*refresh_result), routing_runtime_active_, "startup-retry");
                } catch (const std::exception& e) {
                    Logger::instance().error("Startup lists retry reload failed: {}", e.what());
                }
            }

            // The retry timer is a one-shot that has already fired: schedule
            // the next attempt here while failures remain.
            if (failures_remain) {
                ++startup_list_retry_attempt_;
                schedule_startup_list_retry();
            } else {
                startup_list_retry_attempt_ = 0;
                Logger::instance().info("Startup lists: all remote lists are now available.");
            }
        },
        "startup-list-retry-commit");
}

void Daemon::require_balance_support(const Config& config) const {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    (void)config;  // config validation rejects balance in this build
    return;
#else
    const auto outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    const bool uses_balance = std::any_of(
        outbounds.begin(), outbounds.end(), [](const Outbound& outbound) {
            return (outbound.type == OutboundType::URLTEST ||
                    outbound.type == OutboundType::ICMPTEST) &&
                   outbound_uses_balance(outbound);
        });
    require_iptables_balance_support(firewall_->backend(), uses_balance);
#endif
}

PreparedRuntimeInputs Daemon::prepare_runtime_inputs(const Config& config,
                                                     bool refresh_remote_lists) {
    TraceSpan span("prepare-runtime-inputs");
    validate_config(config, ConfigValidationMode::Runtime, validation_context());
    // Before the config is committed or any routing/firewall state changes:
    // a missing kernel match rejects the apply and the old runtime keeps
    // serving.
    require_balance_support(config);

    PreparedRuntimeInputs prepared;
    prepared.config = config;
    prepared.outbound_marks = allocate_outbound_marks(
        config.fwmark.value_or(FwmarkConfig{}),
        config.outbounds.value_or(std::vector<Outbound>{}));

    if (refresh_remote_lists) {
        // Preparation runs while the current runtime is still active. Use only
        // marks that runtime can actually route; a newly introduced detour is
        // downloaded through the current/default route until reconciliation.
        const auto refresh = list_service_.download_uncached(
            prepared.config, config_store_.outbound_marks());
        if (refresh.any_failed()) {
            throw DaemonError("Failed to prepare remote list(s): " +
                              format_list_names(refresh.failed_lists));
        }
        prepared.remote_lists_refreshed = true;
    }

    return prepared;
}

void Daemon::apply_prepared_runtime_inputs(PreparedRuntimeInputs prepared,
                                           bool publish_active_snapshot,
                                           bool defer_dnsmasq_sync) {
    reconcile_prepared_runtime(std::move(prepared));
    complete_running_runtime("config apply complete", defer_dnsmasq_sync);
    if (publish_active_snapshot) {
        config_store_.replace_active(config_, outbound_marks_);
        publish_runtime_state();
    }
}

void Daemon::reconcile_prepared_runtime(PreparedRuntimeInputs prepared) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("reconcile_prepared_runtime must run on the control/event-loop thread");
    }

    const Config old_config = config_;
    const OutboundMarkMap old_marks = outbound_marks_;
    const auto firewall_policy = firewall_config_apply_policy(
        firewall_->backend(), config_, prepared.config);
    if (firewall_policy.force_clear_dynamic_sets) {
        Logger::instance().warn(
            "iptables ipset capacity changed; recreating owned ipsets and "
            "clearing learned entries");
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);
    // A config change may alter the interception setup: retry listener binds
    // that failed.  Capabilities are not probed again (cached from start).
    intercept_listener_results_.forget_blocking_listener_results();

    if (lists_autoupdate_task_id_ >= 0) {
        scheduler_->cancel(lists_autoupdate_task_id_);
        lists_autoupdate_task_id_ = -1;
    }
    cancel_startup_list_retry();
    if (interface_refresh_task_id_ >= 0) {
        scheduler_->cancel(interface_refresh_task_id_);
        interface_refresh_task_id_ = -1;
    }
    interface_refresh_pending_ = false;
    outbound_marks_ = std::move(prepared.outbound_marks);
    config_ = std::move(prepared.config);
    const auto daemon_config = config_.daemon.value_or(DaemonConfig{});
    set_safe_exec_timeouts(
        std::chrono::seconds{daemon_config.exec_timeout_seconds.value_or(30)},
        std::chrono::seconds{daemon_config.exec_kill_grace_seconds.value_or(2)});
    firewall_state_.set_outbound_marks(outbound_marks_);
    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    pending_urltest_conntrack_cleanup_.clear();
    balance_classifier_cache_.clear();
    const auto main_routes = netlink_.dump_routes_in_table(254);
    reconcile_static_routing(nullptr, &main_routes);
    apply_firewall(firewall_policy.mode,
                   firewall_policy.force_clear_dynamic_sets,
                   &main_routes,
                   &old_config,
                   &old_marks);
    routing_runtime_active_ = true;
    transition_runtime_or_throw(RuntimeState::applying, "config apply");
    begin_runtime_generation();
    publish_runtime_state();
}

void Daemon::apply_config(Config config, bool refresh_remote_lists) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("apply_config must run on the control/event-loop thread");
    }

    try {
        apply_prepared_runtime_inputs(prepare_runtime_inputs(config, refresh_remote_lists));
    } catch (...) {
        config_reload_errors_.fetch_add(1, std::memory_order_relaxed);
        std::string ignored_error;
        (void)runtime_state_machine_.transition(RuntimeState::broken, "config apply failed", ignored_error);
        throw;
    }
}

void Daemon::reload_from_disk() {
    std::ifstream ifs(config_path_);
    if (!ifs.is_open()) {
        config_reload_errors_.fetch_add(1, std::memory_order_relaxed);
        throw DaemonError("Cannot open config file: " + config_path_);
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    const std::string disk_text = ss.str();
    Config next_config;
    try {
        next_config = parse_config(disk_text);
        validate_config(next_config, ConfigValidationMode::Runtime,
                        validation_context());
    } catch (...) {
        config_reload_errors_.fetch_add(1, std::memory_order_relaxed);
        throw;
    }
    upgrade_config_file_if_needed(config_path_, disk_text);
    try {
        apply_config(std::move(next_config));
    } catch (...) {
        std::string ignored_error;
        (void)runtime_state_machine_.transition(
            RuntimeState::broken, "disk reload failed", ignored_error);
        publish_runtime_state();
        throw;
    }
}

} // namespace keen_pbr3
