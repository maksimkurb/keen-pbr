#include "daemon.hpp"
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
#include "../log/logger.hpp"
#include "../routing/urltest_manager.hpp"

#include "../routing/routing_reconciler.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/time_utils.hpp"
#include "../util/cron.hpp"
#include "scheduler.hpp"
#include "system_resolver_hook.hpp"
#include "resolver_stream_wait.hpp"

namespace keen_pbr3 {

bool Daemon::run_system_resolver_hook(std::string_view action) {
    auto& log = Logger::instance();

    std::string command;
    int exit_code = 0;
    bool ok = false;
    const auto args = build_system_resolver_hook_args(config_, action);
    if (args.empty()) {
        command.clear();
        exit_code = 0;
        ok = true;
    } else {
        for (std::size_t index = 0; index < args.size(); ++index) {
            if (index != 0) command += ' ';
            command += args[index];
        }
        auto execute_hook = [this, args] {
            KPBR_LOCK_GUARD(system_resolver_hook_mutex_);
            ipc_resolver_hook_inflight_.store(true, std::memory_order_release);
            try {
                const int result = hook_command_executor_(args);
                ipc_resolver_hook_inflight_.store(false, std::memory_order_release);
                return result;
            } catch (...) {
                ipc_resolver_hook_inflight_.store(false, std::memory_order_release);
                throw;
            }
        };
        if (is_event_loop_thread()) {
            // Some runtime reconfiguration paths still originate on the
            // event-loop thread. Run the external hook on the existing
            // bounded executor and service only its resolver stream while
            // waiting. This avoids both deadlock and an extra thread/stack.
            auto hook_result = resolver_hook_executor_.submit(
                "system-resolver-hook-command", std::move(execute_hook));
            while (hook_result.wait_for(std::chrono::milliseconds{10}) !=
                   std::future_status::ready) {
                handle_ipc_control_socket();
            }
            exit_code = hook_result.get();
        } else {
            exit_code = execute_hook();
        }
        ok = exit_code == 0;
    }

    if (command.empty()) {
        return true;
    }

    if (!ok) {
        log.warn("System resolver reload hook failed (exit code: {}): {}",
                 exit_code,
                 command);
        return false;
    }

    log.info("System resolver hook complete: {}", command);
    return true;
}

bool Daemon::run_system_resolver_hook_reload() {
    const auto stream_baseline = resolver_stream_completed_.load(std::memory_order_acquire);
    if (!run_system_resolver_hook("reload")) {
        return false;
    }
    const auto timeout = resolver_ready_timeout(config_);
    if (!wait_for_resolver_stream_after(stream_baseline, timeout)) {
        const auto stream_current =
            resolver_stream_completed_.load(std::memory_order_acquire);
        Logger::instance().warn(
            "Timed out after {} seconds waiting for dnsmasq resolver "
            "configuration generation to complete after reload (resolver stream "
            "completions baseline={}, current={})",
            timeout.count(), stream_baseline, stream_current);
        return false;
    }
    // The init script may return before dnsmasq invokes its conf-script. The
    // completed-stream wait above makes this the true reload boundary.
    // Recompute only the tiny expected hash afterwards, so confirmation uses
    // the current list/DNS cache state without retaining the large config.
    update_resolver_config_hash();
    return true;
}

bool Daemon::wait_for_resolver_stream_after(std::uint64_t baseline,
                                            std::chrono::seconds timeout) {
    return keen_pbr3::wait_for_resolver_stream_after(
        baseline, timeout,
        [this] {
            return resolver_stream_completed_.load(std::memory_order_acquire);
        },
        [this] {
            if (is_event_loop_thread()) {
                handle_ipc_control_socket();
            }
        },
        wait_for_resolver_stream_poll,
        resolver_stream_now);
}

void Daemon::drain_shutdown_resolver_callbacks(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        handle_ipc_control_socket();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
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
    if (has_system_resolver(config_) && !run_system_resolver_hook("deactivate")) {
        throw DaemonError("System resolver deactivate hook failed");
    }
    refresh_resolver_config_hash_actual_async();
    Logger::instance().info("Routing runtime stopped.");
}

bool Daemon::has_system_resolver(const Config& config) const {
    return config.dns.has_value() && config.dns->system_resolver.has_value() &&
           !config.dns->system_resolver->address.empty();
}

void Daemon::teardown_routing_and_firewall(bool explicit_stop) {
    auto& log = Logger::instance();
    if (!routing_runtime_active_) {
        return;
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    pending_urltest_conntrack_cleanup_.clear();
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
    if (keenetic_dns_refresh_task_id_ >= 0) {
        scheduler_->cancel(keenetic_dns_refresh_task_id_);
        keenetic_dns_refresh_task_id_ = -1;
    }

    routing_runtime_active_ = false;
    transition_runtime_or_throw(explicit_stop ? RuntimeState::stopped : RuntimeState::applying,
                                explicit_stop ? "runtime stopped" : "runtime restarting");
    publish_runtime_state();
    log.info("Routing and firewall stopped.");
}

void Daemon::start_routing_runtime() {
    setup_routing_and_firewall();
    if (has_system_resolver(config_)) {
        if (!run_system_resolver_hook_reload()) {
            throw DaemonError("system resolver reload hook failed");
        }
        std::string error;
        const auto snapshot = resolver_sync_.snapshot(unix_timestamp_now_seconds());
        if (!wait_for_resolver_config_hash_confirmation(
                config_, snapshot.expected_hash,
                apply_started_ts_.load(std::memory_order_acquire), error)) {
            throw DaemonError(error);
        }
    }
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
    (void)refresh_keenetic_dns_cache(true);
    apply_firewall(FirewallApplyMode::Destructive, false, &main_routes);
    routing_runtime_active_ = true;
    if (runtime_state_machine_.state() != RuntimeState::applying) {
        transition_runtime_or_throw(RuntimeState::applying, "runtime starting");
    }
    publish_runtime_state();
    apply_started_ts_.store(unix_timestamp_now_seconds(), std::memory_order_release);
    update_resolver_config_hash();
    setup_dns_probe();
}

void Daemon::complete_running_runtime(const char* reason) {
    register_urltest_outbounds();
    schedule_keenetic_dns_refresh();
    schedule_lists_autoupdate();
    refresh_resolver_config_hash_actual_async();
    transition_runtime_or_throw(RuntimeState::running, reason);
    publish_runtime_state();
}

void Daemon::restart_routing_runtime() {
    if (!routing_runtime_active_) {
        throw DaemonError("Routing runtime is stopped");
    }

    teardown_routing_and_firewall(false);
    setup_routing_and_firewall();
    if (has_system_resolver(config_)) {
        if (!run_system_resolver_hook_reload()) {
            throw DaemonError("system resolver reload hook failed");
        }
        std::string error;
        const auto snapshot = resolver_sync_.snapshot(unix_timestamp_now_seconds());
        if (!wait_for_resolver_config_hash_confirmation(
                config_, snapshot.expected_hash,
                apply_started_ts_.load(std::memory_order_acquire), error)) {
            throw DaemonError(error);
        }
    }
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
                            const std::vector<DumpedRoute>* main_routes) {
    const FirewallGlobalPrefilter prefilter = build_firewall_global_prefilter(config_);
    const auto owned_main_routes = main_routes != nullptr
        ? *main_routes
        : netlink_.dump_routes_in_table(254);
    const auto interfaces = netlink_.dump_interfaces();
    const auto balance_candidates = build_balance_candidates(owned_main_routes, interfaces);
    firewall_state_.set_rules(apply_runtime_firewall(
        config_,
        outbound_marks_,
        list_service_.cache_manager(),
        *firewall_,
        mode,
        &firewall_state_.get_rules(),
        force_clear_dynamic_sets,
        owned_main_routes,
        interfaces,
        &balance_candidates));
    (void)conntrack_manager_.reconcile(
        ConntrackPolicy{prefilter.skip_established_or_dnat});
}

FirewallBalanceCandidates Daemon::build_balance_candidates(
    const std::vector<DumpedRoute>& main_routes,
    const std::vector<DumpedInterface>& interfaces) {
    FirewallBalanceCandidates candidates;
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
}

void Daemon::reconcile_lists_only(bool reload_resolver) {
    if (!routing_runtime_active_) {
        throw DaemonError("list-only reconcile requires an active routing runtime");
    }

    try {
        apply_firewall(FirewallApplyMode::StaticSetsOnly);
        if (reload_resolver) {
            update_resolver_config_hash();
            if (!run_system_resolver_hook_reload()) {
                throw DaemonError("system resolver reload hook failed");
            }
        }
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
            const bool delete_on_healthy_switch =
                configured->conntrack_on_switch.value_or(api::ConntrackOnSwitch::PRESERVE) ==
                api::ConntrackOnSwitch::DELETE;
            for (const auto& group : configured->outbound_groups.value_or(
                     std::vector<OutboundGroup>{})) {
                for (const auto& child_tag : outbound_group_tags(group)) {
                    const auto mark = outbound_marks_.find(child_tag);
                    if (mark == outbound_marks_.end()) continue;
                    if (child_failed(child_tag) ||
                        (delete_on_healthy_switch && !old_child_tag.empty() &&
                         old_child_tag != new_child_tag)) {
                        cleanup_marks.insert(mark->second);
                    }
                }
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
                log.info("Updated nft balance classifier for test-group '{}' ({} child marks cleaned)",
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

ListsRefreshExecutionResult Daemon::execute_remote_list_refresh(
    const std::set<std::string>* target_lists,
    std::string_view source) {
    auto& log = Logger::instance();
    ListsRefreshExecutionResult result;
    const auto relevant_lists = collect_relevant_list_names(config_);
    const auto dns_relevant_lists = collect_dns_relevant_list_names(config_);
    result.refresh_result =
        list_service_.refresh_remote_lists(
            config_, outbound_marks_, &relevant_lists, target_lists, &dns_relevant_lists);

    if (!result.refresh_result.changed_lists.empty()) {
        log.info("Lists refresh ({}): updated list(s): {}", source,
                 format_list_names(result.refresh_result.changed_lists));
    } else if (!result.refresh_result.failed_lists.empty()) {
        log.warn("Lists refresh ({}): failed list(s): {}", source,
                 format_list_names(result.refresh_result.failed_lists));
    } else {
        log.info("Lists refresh ({}): all checked list(s) are up-to-date.", source);
    }

    if (should_reload_runtime_after_list_refresh(routing_runtime_active_, result.refresh_result)) {
        log.info("Lists refresh ({}): relevant list(s) changed ({}), reloading runtime",
                 source,
                 format_list_names(result.refresh_result.relevant_changed_lists));
        reconcile_lists_only(result.refresh_result.any_dns_relevant_changed());
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

void Daemon::refresh_lists_and_maybe_reload() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    try {
        const auto result = execute_remote_list_refresh(nullptr, "autoupdate");
        if (!result.reloaded) {
            schedule_lists_autoupdate();
        }
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

            ListsRefreshExecutionResult result;
            result.refresh_result = std::move(*refresh_result);

            if (!result.refresh_result.changed_lists.empty()) {
                Logger::instance().info("Lists refresh (autoupdate): updated list(s): {}",
                                        format_list_names(result.refresh_result.changed_lists));
            } else if (!result.refresh_result.failed_lists.empty()) {
                Logger::instance().warn("Lists refresh (autoupdate): failed list(s): {}",
                                        format_list_names(result.refresh_result.failed_lists));
            } else {
                Logger::instance().info(
                    "Lists refresh (autoupdate): all checked list(s) are up-to-date.");
            }

            if (should_reload_runtime_after_list_refresh(runtime_active_snapshot,
                                                        result.refresh_result)) {
                Logger::instance().info(
                    "Lists refresh (autoupdate): relevant list(s) changed ({}), reloading runtime",
                    format_list_names(result.refresh_result.relevant_changed_lists));
                try {
                    reconcile_lists_only(result.refresh_result.any_dns_relevant_changed());
                    result.reloaded = true;
                } catch (const std::exception& e) {
                    Logger::instance().error("Lists autoupdate reload failed: {}", e.what());
                    schedule_lists_autoupdate();
                    return;
                }
            } else if (result.refresh_result.any_relevant_changed()) {
                Logger::instance().info(
                    "Lists refresh: relevant list(s) changed ({}), but runtime is stopped",
                    format_list_names(result.refresh_result.relevant_changed_lists));
            } else if (result.refresh_result.any_changed()) {
                Logger::instance().info(
                    "Lists refresh: updated list(s) did not affect runtime config: {}",
                    format_list_names(result.refresh_result.changed_lists));
            } else if (result.refresh_result.any_failed()) {
                Logger::instance().warn("Lists refresh: failed to refresh list(s): {}",
                                        format_list_names(result.refresh_result.failed_lists));
            } else {
                Logger::instance().info("Lists refresh: no list updates");
            }

            if (!result.reloaded) {
                schedule_lists_autoupdate();
            }
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
    const auto dns_relevant_lists = collect_dns_relevant_list_names(config_snapshot);
    const auto generation = runtime_generation_.load(std::memory_order_acquire);
    const TraceId trace_id = ensure_trace_id();

    const bool enqueued = blocking_executor_.try_post(
        "lists-autoupdate",
        [this,
         config_snapshot,
         marks_snapshot,
         runtime_active_snapshot,
         relevant_lists,
         dns_relevant_lists,
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
                                                                   nullptr,
                                                                   &dns_relevant_lists);
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

PreparedRuntimeInputs Daemon::prepare_runtime_inputs(const Config& config,
                                                     bool refresh_remote_lists) {
    TraceSpan span("prepare-runtime-inputs");
    validate_config(config);

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
                                           bool publish_active_snapshot) {
    reconcile_prepared_runtime(std::move(prepared));
    if (has_system_resolver(config_) && !run_system_resolver_hook_reload()) {
        throw DaemonError("system resolver reload hook failed");
    }
    if (has_system_resolver(config_)) {
        std::string error;
        const auto snapshot = resolver_sync_.snapshot(unix_timestamp_now_seconds());
        if (!wait_for_resolver_config_hash_confirmation(
                config_, snapshot.expected_hash,
                apply_started_ts_.load(std::memory_order_acquire), error)) {
            throw DaemonError(error);
        }
    }
    complete_running_runtime("config apply complete");
    if (publish_active_snapshot) {
        config_store_.replace_active(config_, outbound_marks_);
        publish_runtime_state();
    }
}

void Daemon::reconcile_prepared_runtime(PreparedRuntimeInputs prepared) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("reconcile_prepared_runtime must run on the control/event-loop thread");
    }

    const auto firewall_policy = firewall_config_apply_policy(
        firewall_->backend(), config_, prepared.config);
    if (firewall_policy.force_clear_dynamic_sets) {
        Logger::instance().warn(
            "iptables ipset capacity changed; recreating owned ipsets and "
            "clearing dnsmasq-learned entries");
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    if (lists_autoupdate_task_id_ >= 0) {
        scheduler_->cancel(lists_autoupdate_task_id_);
        lists_autoupdate_task_id_ = -1;
    }
    if (keenetic_dns_refresh_task_id_ >= 0) {
        scheduler_->cancel(keenetic_dns_refresh_task_id_);
        keenetic_dns_refresh_task_id_ = -1;
    }
    if (resolver_config_hash_actual_task_id_ >= 0) {
        scheduler_->cancel(resolver_config_hash_actual_task_id_);
        resolver_config_hash_actual_task_id_ = -1;
    }
    if (resolver_config_hash_actual_retry_task_id_ >= 0) {
        scheduler_->cancel(resolver_config_hash_actual_retry_task_id_);
        resolver_config_hash_actual_retry_task_id_ = -1;
    }

    outbound_marks_ = std::move(prepared.outbound_marks);
    config_ = std::move(prepared.config);
    const auto daemon_config = config_.daemon.value_or(DaemonConfig{});
    set_safe_exec_timeouts(
        std::chrono::seconds{daemon_config.exec_timeout_seconds.value_or(30)},
        std::chrono::seconds{daemon_config.exec_kill_grace_seconds.value_or(2)});
    firewall_state_.set_outbound_marks(outbound_marks_);
    firewall_state_.set_fwmark_mask(fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{})));

    teardown_dns_probe();

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    pending_urltest_conntrack_cleanup_.clear();
    const auto main_routes = netlink_.dump_routes_in_table(254);
    reconcile_static_routing(nullptr, &main_routes);
    (void)refresh_keenetic_dns_cache(true);
    apply_firewall(firewall_policy.mode,
                   firewall_policy.force_clear_dynamic_sets,
                   &main_routes);
    routing_runtime_active_ = true;
    transition_runtime_or_throw(RuntimeState::applying, "config apply");
    apply_started_ts_.store(unix_timestamp_now_seconds(), std::memory_order_release);
    update_resolver_config_hash();
    setup_dns_probe();
    publish_runtime_state();
}

void Daemon::apply_config(Config config, bool refresh_remote_lists) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("apply_config must run on the control/event-loop thread");
    }

    try {
        apply_prepared_runtime_inputs(prepare_runtime_inputs(config, refresh_remote_lists));
    } catch (...) {
        std::string ignored_error;
        (void)runtime_state_machine_.transition(RuntimeState::broken, "config apply failed", ignored_error);
        throw;
    }
}

void Daemon::reload_from_disk() {
    std::ifstream ifs(config_path_);
    if (!ifs.is_open()) {
        throw DaemonError("Cannot open config file: " + config_path_);
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    Config next_config = parse_config(ss.str());
    validate_config(next_config);
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
