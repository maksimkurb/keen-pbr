#include "daemon.hpp"

#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "../config/addr_spec.hpp"
#include "../config/routing_state.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dns_server.hpp"
#include "../firewall/firewall.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_set_usage.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../routing/urltest_manager.hpp"
#include "../util/time_utils.hpp"
#include "../util/cron.hpp"
#include "scheduler.hpp"
#include "system_resolver_hook.hpp"

namespace keen_pbr3 {

namespace {

std::string format_list_names(const std::vector<std::string>& list_names) {
    if (list_names.empty()) {
        return "(none)";
    }

    std::ostringstream out;
    for (size_t i = 0; i < list_names.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << list_names[i];
    }
    return out.str();
}

L4Proto parse_rule_proto(const std::optional<std::string>& proto) {
    if (!proto.has_value() || proto->empty()) return L4Proto::Any;
    if (*proto == "tcp") return L4Proto::Tcp;
    if (*proto == "udp") return L4Proto::Udp;
    if (*proto == "tcp/udp") return L4Proto::TcpUdp;
    throw DaemonError("Unsupported route rule protocol: " + *proto);
}

} // namespace

void Daemon::run_system_resolver_hook_reload() {
    auto& log = Logger::instance();

    std::string command;
    int exit_code = 0;
    const bool ok = execute_system_resolver_reload_hook(
        config_,
        hook_command_executor_,
        command,
        exit_code);

    if (command.empty()) {
        return;
    }

    if (!ok) {
        log.warn("System resolver reload hook failed (exit code: {}): {}",
                 exit_code,
                 command);
        return;
    }

    log.info("System resolver reload hook complete: {}", command);
}

bool Daemon::routing_runtime_active() const {
    return runtime_state_store_.snapshot().routing_runtime_active;
}

void Daemon::stop_routing_runtime() {
    auto& log = Logger::instance();
    if (!routing_runtime_active_) {
        return;
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();
    if (keenetic_dns_refresh_task_id_ >= 0) {
        scheduler_->cancel(keenetic_dns_refresh_task_id_);
        keenetic_dns_refresh_task_id_ = -1;
    }

    if (config_.dns.has_value() && config_.dns->system_resolver.has_value()) {
        const auto args = build_system_resolver_hook_args(config_, "deactivate");
        const int exit_code = hook_command_executor_(args);
        if (exit_code != 0) {
            throw DaemonError("System resolver deactivate hook failed with exit code " +
                              std::to_string(exit_code));
        }
    }

    routing_runtime_active_ = false;
    refresh_resolver_config_hash_actual_async();
    publish_runtime_state();
    log.info("Routing runtime stopped.");
}

void Daemon::start_routing_runtime() {
    auto& log = Logger::instance();
    if (routing_runtime_active_) {
        return;
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    setup_static_routing();
    register_urltest_outbounds();
    (void)refresh_keenetic_dns_cache(true);
    apply_firewall();

    if (config_.dns.has_value() && config_.dns->system_resolver.has_value()) {
        auto args = build_system_resolver_hook_args(config_, "ensure-runtime-prereqs");
        int exit_code = hook_command_executor_(args);
        if (exit_code != 0) {
            throw DaemonError("System resolver ensure-runtime-prereqs hook failed with exit code " +
                              std::to_string(exit_code));
        }
        args = build_system_resolver_hook_args(config_, "activate");
        exit_code = hook_command_executor_(args);
        if (exit_code != 0) {
            throw DaemonError("System resolver activate hook failed with exit code " +
                              std::to_string(exit_code));
        }
    }

    routing_runtime_active_ = true;
    apply_started_ts_.store(unix_timestamp_now_seconds(), std::memory_order_release);
    schedule_keenetic_dns_refresh();
    refresh_resolver_config_hash_actual_async();
    publish_runtime_state();
    log.info("Routing runtime started.");
}

void Daemon::restart_routing_runtime() {
    if (!routing_runtime_active_) {
        throw DaemonError("Routing runtime is stopped");
    }

    apply_started_ts_.store(unix_timestamp_now_seconds(), std::memory_order_release);
    stop_routing_runtime();
    start_routing_runtime();
}

void Daemon::setup_static_routing() {
    populate_routing_state(
        config_,
        outbound_marks_,
        route_table_,
        policy_rules_,
        [this](const Outbound& outbound) {
            return is_interface_outbound_reachable(outbound, netlink_);
        },
        &firewall_state_.get_urltest_selections());
}

void Daemon::apply_firewall() {
    ListStreamer list_streamer(list_service_.cache_manager());
    auto rule_states =
        build_fw_rule_states(config_, outbound_marks_, &firewall_state_.get_urltest_selections());
    const RouteConfig route_config = config_.route.value_or(RouteConfig{});

    firewall_->cleanup();
    firewall_->set_global_prefilter(build_firewall_global_prefilter(config_));

    const auto& all_outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config_.lists ? *config_.lists : empty_lists;
    const auto& route_rules = route_config.rules.value_or(std::vector<RouteRule>{});
    std::map<std::string, ListSetUsage> list_usage_cache;

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];
        RuleState& rs = rule_states[rule_idx];

        if (rs.action_type == RuleActionType::Skip) {
            continue;
        }

        rs.set_names.clear();

        const bool is_blackhole = (rs.action_type == RuleActionType::Drop);
        const bool is_pass = (rs.action_type == RuleActionType::Pass);
        auto strip_neg = [](const std::string& s) -> std::pair<std::string, bool> {
            if (!s.empty() && s[0] == '!') return {s.substr(1), true};
            return {s, false};
        };

        FirewallRuleCriteria criteria;
        criteria.proto = parse_rule_proto(rule.proto);

        {
            auto [port, neg] = strip_neg(rule.src_port.value_or(""));
            criteria.src_port = port;
            criteria.negate_src_port = neg;
        }
        {
            auto [port, neg] = strip_neg(rule.dest_port.value_or(""));
            criteria.dst_port = port;
            criteria.negate_dst_port = neg;
        }
        {
            AddrSpec s = parse_addr_spec(rule.src_addr.value_or(""));
            criteria.negate_src_addr = s.negate;
            criteria.src_addr = std::move(s.addrs);
        }
        {
            AddrSpec s = parse_addr_spec(rule.dest_addr.value_or(""));
            criteria.negate_dst_addr = s.negate;
            criteria.dst_addr = std::move(s.addrs);
        }

        auto apply_rule = [&](const std::optional<std::string>& dst_set_name) {
            FirewallRuleCriteria rule_criteria = criteria;
            rule_criteria.dst_set_name = dst_set_name;

            if (is_blackhole) {
                firewall_->create_drop_rule(rule_criteria);
            } else if (is_pass) {
                firewall_->create_pass_rule(rule_criteria);
            } else if (rs.fwmark != 0) {
                firewall_->create_mark_rule(rs.fwmark, rule_criteria);
            }
        };

        const auto& list_names = route_rule_lists(rule);
        if (!list_names.empty()) {
            bool emitted_rule = false;

            for (const auto& list_name : list_names) {
                auto list_cfg_it = lists_map.find(list_name);
                if (list_cfg_it == lists_map.end()) continue;

                const auto& list_cfg = list_cfg_it->second;
                auto usage_it = list_usage_cache.find(list_name);
                if (usage_it == list_usage_cache.end()) {
                    usage_it = list_usage_cache.emplace(
                        list_name,
                        analyze_list_set_usage(list_name, list_cfg, list_streamer)).first;
                }
                const auto& usage = usage_it->second;

                const std::string set4 = "kpbr4_"  + list_name;
                const std::string set6 = "kpbr6_"  + list_name;
                const std::string set4d = "kpbr4d_" + list_name;
                const std::string set6d = "kpbr6d_" + list_name;

                if (usage.has_static_entries) {
                    firewall_->create_ipset(set4, AF_INET, 0);
                    firewall_->create_ipset(set6, AF_INET6, 0);
                    rs.set_names.push_back(set4);
                    rs.set_names.push_back(set6);

                    auto loader4 = firewall_->create_batch_loader(set4);
                    auto loader6 = firewall_->create_batch_loader(set6);
                    FunctionalVisitor splitter([&](EntryType type, std::string_view entry) {
                        if (type == EntryType::Domain) return;
                        bool is_v6 = entry.find(':') != std::string_view::npos;
                        if (is_v6) loader6->on_entry(type, entry);
                        else       loader4->on_entry(type, entry);
                    });
                    list_streamer.stream_list(list_name, list_cfg, splitter);
                    loader4->finish();
                    loader6->finish();
                }

                if (usage.has_domain_entries) {
                    firewall_->create_ipset(set4d, AF_INET, usage.dynamic_timeout);
                    firewall_->create_ipset(set6d, AF_INET6, usage.dynamic_timeout);
                    rs.set_names.push_back(set4d);
                    rs.set_names.push_back(set6d);
                }
                if (usage.has_static_entries) {
                    apply_rule(set4);
                    apply_rule(set6);
                    emitted_rule = true;
                }
                if (usage.has_domain_entries) {
                    apply_rule(set4d);
                    apply_rule(set6d);
                    emitted_rule = true;
                }
            }

            if (!emitted_rule && criteria.has_rule_selector()) {
                apply_rule(std::nullopt);
            }
        } else if (criteria.has_rule_selector()) {
            apply_rule(std::nullopt);
        }
    }

    if (config_.dns.has_value()) {
        const auto& dns_servers = config_.dns->servers.value_or(std::vector<DnsServer>{});
        const DnsServerRegistry dns_registry(config_.dns.value_or(DnsConfig{}));
        for (const auto& srv : dns_servers) {
            if (!srv.detour.has_value()) continue;

            const Outbound* detour_ob = find_outbound(all_outbounds, srv.detour.value());
            if (!detour_ob) continue;

            std::string effective_tag = detour_ob->tag;
            if (detour_ob->type == OutboundType::URLTEST) {
                auto selections = firewall_state_.get_urltest_selections();
                auto sel_it = selections.find(effective_tag);
                if (sel_it != selections.end() && !sel_it->second.empty()) {
                    const Outbound* child = find_outbound(all_outbounds, sel_it->second);
                    if (child) effective_tag = child->tag;
                }
            }

            auto mark_it = outbound_marks_.find(effective_tag);
            if (mark_it == outbound_marks_.end()) continue;

            const auto resolved_servers = dns_registry.get_servers(srv.tag);
            if (resolved_servers.empty()) {
                throw DaemonError("DNS server tag not found during detour setup: " + srv.tag);
            }
            for (const DnsServerConfig* resolved_server : resolved_servers) {
                FirewallRuleCriteria criteria;
                criteria.proto = L4Proto::TcpUdp;
                criteria.dst_port = std::to_string(resolved_server->port);
                criteria.dst_addr = {resolved_server->resolved_ip};
                firewall_->create_mark_rule(mark_it->second, criteria);
            }
        }
    }

    firewall_->apply();
    firewall_state_.set_rules(std::move(rule_states));
}

void Daemon::download_uncached_lists() {
    list_service_.download_uncached(config_, outbound_marks_);
}

void Daemon::handle_urltest_selection_change(const std::string& urltest_tag,
                                             const std::string& new_child_tag) {
    post_control_task([this, urltest_tag, new_child_tag]() {
        auto& log = Logger::instance();
        log.info("Urltest '{}' selected outbound: '{}'", urltest_tag, new_child_tag);
        firewall_state_.set_urltest_selection(urltest_tag, new_child_tag);
        try {
            route_table_.clear();
            policy_rules_.clear();
            setup_static_routing();
            apply_firewall();
            publish_runtime_state();
            log.info("Routing and firewall rebuilt after urltest change.");
        } catch (const std::exception& e) {
            log.error("Error rebuilding routing/firewall after urltest change: {}", e.what());
        }
    }, "urltest-selection-change:" + urltest_tag);
}

void Daemon::commit_urltest_probe_results(const std::string& urltest_tag,
                                          std::uint64_t probe_generation,
                                          std::map<std::string, URLTestResult> results,
                                          TraceId trace_id) {
    post_control_task(
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
            publish_runtime_state();
        },
        "urltest-commit:" + urltest_tag);
}

void Daemon::register_urltest_outbounds() {
    if (!urltest_manager_) {
        urltest_manager_ = std::make_unique<UrltestManager>(
            url_tester_,
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
                commit_urltest_probe_results(urltest_tag,
                                             probe_generation,
                                             std::move(results),
                                             trace_id);
            });
    }

    for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
        if (ob.type == OutboundType::URLTEST) {
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
    const std::set<std::string>* target_lists) {
    auto& log = Logger::instance();
    ListsRefreshExecutionResult result;
    const auto relevant_lists = collect_relevant_list_names(config_);
    result.refresh_result =
        list_service_.refresh_remote_lists(config_, outbound_marks_, &relevant_lists, target_lists);

    if (should_reload_runtime_after_list_refresh(routing_runtime_active_, result.refresh_result)) {
        log.info("Lists refresh: relevant list(s) changed ({}), reloading runtime",
                 format_list_names(result.refresh_result.relevant_changed_lists));
        apply_config(config_, false);
        result.reloaded = true;
        return result;
    }

    if (result.refresh_result.any_relevant_changed()) {
        log.info("Lists refresh: relevant list(s) changed ({}), but runtime is stopped",
                 format_list_names(result.refresh_result.relevant_changed_lists));
    } else if (result.refresh_result.any_changed()) {
        log.info("Lists refresh: updated list(s) did not affect runtime config: {}",
                 format_list_names(result.refresh_result.changed_lists));
    } else {
        log.info("Lists refresh: no list updates");
    }

    return result;
}

void Daemon::refresh_lists_and_maybe_reload() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    try {
        const auto result = execute_remote_list_refresh();
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

            if (should_reload_runtime_after_list_refresh(runtime_active_snapshot,
                                                        result.refresh_result)) {
                Logger::instance().info(
                    "Lists refresh: relevant list(s) changed ({}), reloading runtime",
                    format_list_names(result.refresh_result.relevant_changed_lists));
                try {
                    apply_config(config_snapshot, false);
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
                                                                   &relevant_lists);
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
        (void)list_service_.refresh_remote_lists(prepared.config, prepared.outbound_marks);
        prepared.remote_lists_refreshed = true;
    }

    return prepared;
}

void Daemon::apply_prepared_runtime_inputs(PreparedRuntimeInputs prepared) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("apply_prepared_runtime_inputs must run on the control/event-loop thread");
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
    firewall_state_.set_outbound_marks(outbound_marks_);

    teardown_dns_probe();

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();

    setup_static_routing();
    register_urltest_outbounds();
    (void)refresh_keenetic_dns_cache(true);
    apply_firewall();
    schedule_keenetic_dns_refresh();
    schedule_lists_autoupdate();
    update_resolver_config_hash();
    setup_dns_probe();
    run_system_resolver_hook_reload();
    refresh_resolver_config_hash_actual_async();
    schedule_resolver_config_hash_actual_refresh();

    config_store_.replace_active(config_, outbound_marks_);
    publish_runtime_state();
}

void Daemon::apply_config(Config config, bool refresh_remote_lists) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("apply_config must run on the control/event-loop thread");
    }

    apply_prepared_runtime_inputs(prepare_runtime_inputs(config, refresh_remote_lists));
}

void Daemon::apply_config_with_rollback(const Config& next_config, bool& rolled_back) {
    Config previous_config = config_;

    try {
        apply_config(next_config);
        rolled_back = false;
    } catch (...) {
        try {
            apply_config(previous_config);
            rolled_back = true;
        } catch (const std::exception& rollback_error) {
            Logger::instance().error("Rollback to previous config failed: {}", rollback_error.what());
            rolled_back = false;
        } catch (...) {
            Logger::instance().error("Rollback to previous config failed: unknown error");
            rolled_back = false;
        }
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
    apply_config(std::move(next_config));
}

} // namespace keen_pbr3
