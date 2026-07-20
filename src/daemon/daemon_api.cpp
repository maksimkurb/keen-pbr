#include "daemon.hpp"
#include "config_apply_transaction.hpp"
#include "../config/config_writer.hpp"

#ifdef WITH_API

#include <filesystem>
#include <future>

#include "../api/handlers.hpp"
#include "../api/server.hpp"
#include "../config/routing_state.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../util/ipv6_support.hpp"
#include "../health/routing_health_checker.hpp"
#include "../health/runtime_interface_inventory.hpp"
#include "../health/runtime_outbound_state.hpp"
#include "../keenetic/interface_descriptions.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../util/system_info.hpp"
#include "../util/time_utils.hpp"

#ifndef KEEN_PBR_FRONTEND_ROOT
#define KEEN_PBR_FRONTEND_ROOT "/usr/share/keen-pbr/frontend"
#endif

namespace keen_pbr3 {

namespace {

const char* config_operation_state_name(ConfigOperationState state) {
    switch (state) {
    case ConfigOperationState::Idle:
        return "idle";
    case ConfigOperationState::Saving:
        return "saving";
    case ConfigOperationState::Reloading:
        return "reloading";
    }
    return "unknown";
}

} // namespace

void Daemon::finish_config_operation() {
    KPBR_LOCK_GUARD(config_op_mutex_);
    config_op_state_.store(ConfigOperationState::Idle, std::memory_order_release);
    Logger::instance().trace("config_operation_state",
                             "state={} reason=finish",
                             config_operation_state_name(ConfigOperationState::Idle));
    config_op_cv_.notify_all();
    operation_coordinator_.finish();
}

void Daemon::begin_config_operation_or_throw(ConfigOperationState state,
                                             const char* reason,
                                             bool require_runtime_running,
                                             bool require_runtime_stopped) {
    KPBR_UNIQUE_LOCK(lock, config_op_mutex_);
    if (config_op_state_.load(std::memory_order_acquire) != ConfigOperationState::Idle ||
        !operation_coordinator_.try_begin(reason)) {
        throw ApiError("Another config operation is already in progress", 409);
    }

    const bool runtime_running = runtime_state_store_.snapshot().routing_runtime_active;
    if (require_runtime_running && !runtime_running) {
        operation_coordinator_.finish();
        throw ApiError("Routing runtime is stopped; start it first", 409);
    }
    if (require_runtime_stopped && runtime_running) {
        operation_coordinator_.finish();
        throw ApiError("Routing runtime is already started", 409);
    }

    config_op_state_.store(state, std::memory_order_release);
    Logger::instance().trace("config_operation_state",
                             "state={} reason={}",
                             config_operation_state_name(state),
                             reason);
}

void Daemon::run_runtime_control_operation_or_throw(const std::string& label,
                                                    const char* operation_name,
                                                    std::function<void()> task) {
    try {
        enqueue_control_task(
            [task = std::move(task), operation_name]() {
                try {
                    task();
                } catch (const std::exception& e) {
                    Logger::instance().error("{} task failed: {}", operation_name, e.what());
                    throw;
                }
            },
            true,
            label);
    } catch (...) {
        finish_config_operation();
        throw;
    }

    finish_config_operation();
}

ConfigApplyResult Daemon::apply_validated_config_via_control_task(
    Config config,
    std::string saved_config_json,
    bool persist_config) {
    auto result = std::make_shared<ConfigApplyResult>();
    auto prepared = std::make_shared<PreparedRuntimeInputs>();
    auto rollback_prepared = std::make_shared<PreparedRuntimeInputs>();
    auto completion = std::make_shared<std::promise<ConfigApplyResult>>();
    auto completed = std::make_shared<std::atomic<bool>>(false);
    auto transaction = std::make_shared<ConfigApplyTransaction>();
    auto completion_future = completion->get_future();
    const std::int64_t apply_started_ts = unix_timestamp_now_seconds();
    result->apply_started_ts = apply_started_ts;
    apply_started_ts_.store(apply_started_ts, std::memory_order_release);

    try {
        *prepared = prepare_runtime_inputs(config, true);
        *rollback_prepared = prepare_runtime_inputs(config_store_.active_config(), false);
    } catch (const std::exception& e) {
        result->error = e.what();
        Logger::instance().error("Prepare staged config task failed: {}", e.what());
        return *result;
    }

    enqueue_control_task(
        [this,
         result,
         prepared,
         rollback_prepared,
         completion,
         completed,
         transaction,
         persist_config,
         saved_config_json = std::move(saved_config_json)]() mutable {
            const auto complete = [completion, completed](ConfigApplyResult value) {
                bool expected = false;
                if (completed->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                    completion->set_value(std::move(value));
                }
            };
            auto rollback = [this, result, rollback_prepared, transaction, &complete]() mutable {
                try {
                    transaction->rolled_back();
                    apply_prepared_runtime_inputs(std::move(*rollback_prepared));
                    transition_runtime_or_throw(RuntimeState::running, "config apply rolled back");
                    result->rolled_back = true;
                } catch (const std::exception& rollback_error) {
                    result->rolled_back = false;
                    std::string ignored_error;
                    (void)runtime_state_machine_.transition(
                        RuntimeState::broken, "config rollback failed", ignored_error);
                    publish_runtime_state();
                    Logger::instance().error("Rollback to previous config failed: {}",
                                             rollback_error.what());
                } catch (...) {
                    result->rolled_back = false;
                    std::string ignored_error;
                    (void)runtime_state_machine_.transition(
                        RuntimeState::broken, "config rollback failed", ignored_error);
                    publish_runtime_state();
                    Logger::instance().error("Rollback to previous config failed: unknown error");
                }
                complete(*result);
            };
            try {
                transition_runtime_or_throw(RuntimeState::applying, "config apply");
                apply_prepared_runtime_inputs(std::move(*prepared), false);
                transaction->candidate_applied();
                const std::string expected_hash =
                    resolver_sync_.snapshot(unix_timestamp_now_seconds()).expected_hash;
                const Config candidate = config_;
                const std::int64_t apply_started_ts = result->apply_started_ts.value_or(0);
                const bool queued = blocking_executor_.try_post(
                    "config-apply-resolver-confirmation",
                    [this,
                     result,
                     rollback_prepared,
                     completion,
                     completed,
                     transaction,
                     persist_config,
                     saved_config_json,
                     candidate,
                     expected_hash,
                     apply_started_ts] {
                        std::string confirmation_error;
                        bool confirmed = false;
                        try {
                            confirmed = wait_for_resolver_config_hash_confirmation(
                                candidate, expected_hash, apply_started_ts, confirmation_error);
                        } catch (const std::exception& error) {
                            confirmation_error = error.what();
                        } catch (...) {
                            confirmation_error = "resolver confirmation failed with an unknown error";
                        }
                        post_control_task(
                            [this,
                             result,
                             rollback_prepared,
                             completion,
                             completed,
                             transaction,
                             persist_config,
                             saved_config_json,
                             confirmed,
                             confirmation_error = std::move(confirmation_error)]() mutable {
                                const auto complete_inner = [completion, completed](ConfigApplyResult value) {
                                    bool expected = false;
                                    if (completed->compare_exchange_strong(
                                            expected, true, std::memory_order_acq_rel)) {
                                        completion->set_value(std::move(value));
                                    }
                                };
                                auto rollback_inner = [this,
                                                             result,
                                                             rollback_prepared,
                                                             transaction,
                                                             &complete_inner]() mutable {
                                    try {
                                        transaction->rolled_back();
                                        apply_prepared_runtime_inputs(std::move(*rollback_prepared));
                                        transition_runtime_or_throw(
                                            RuntimeState::running, "config apply rolled back");
                                        result->rolled_back = true;
                                    } catch (const std::exception& rollback_error) {
                                        result->rolled_back = false;
                                        std::string ignored_error;
                                        (void)runtime_state_machine_.transition(
                                            RuntimeState::broken, "config rollback failed", ignored_error);
                                        publish_runtime_state();
                                        Logger::instance().error("Rollback to previous config failed: {}",
                                                                 rollback_error.what());
                                    } catch (...) {
                                        result->rolled_back = false;
                                        std::string ignored_error;
                                        (void)runtime_state_machine_.transition(
                                            RuntimeState::broken, "config rollback failed", ignored_error);
                                        publish_runtime_state();
                                        Logger::instance().error(
                                            "Rollback to previous config failed: unknown error");
                                    }
                                    complete_inner(*result);
                                };
                                if (!confirmed) {
                                    result->error = confirmation_error;
                                    Logger::instance().error("Candidate resolver confirmation failed: {}",
                                                             confirmation_error);
                                    rollback_inner();
                                    return;
                                }
                                try {
                                    transaction->resolver_confirmed();
                                    if (!transaction->may_commit()) {
                                        throw DaemonError("resolver confirmation did not unlock config commit");
                                    }
                                    if (persist_config) {
                                        write_config_atomically(config_path_, saved_config_json);
                                    }
                                    config_store_.replace_active(config_, outbound_marks_);
                                    if (persist_config) {
                                        config_store_.clear_staged_if_matches(saved_config_json);
                                    }
                                    transition_runtime_or_throw(RuntimeState::running,
                                                                "config apply complete");
                                    publish_runtime_state();
                                    result->saved = persist_config;
                                    result->applied = true;
                                    result->rolled_back = false;
                                    transaction->committed();
                                } catch (const std::exception& error) {
                                    result->error = error.what();
                                    Logger::instance().error("Config durable commit failed: {}", error.what());
                                    rollback_inner();
                                    return;
                                }
                                complete_inner(*result);
                            },
                            "config-apply-resolver-confirmation-commit");
                    });
                if (!queued) {
                    throw DaemonError("resolver confirmation executor is unavailable");
                }
            } catch (const std::exception& e) {
                result->error = e.what();
                Logger::instance().error("Apply staged config task failed: {}", e.what());
                rollback();
            }
        },
        false,
        "api-apply-config");
    return completion_future.get();
}

ListRefreshOperationResult Daemon::refresh_lists_via_api(std::optional<std::string> requested_name) {
    begin_config_operation_or_throw(ConfigOperationState::Reloading,
                                    "refresh-lists",
                                    false,
                                    false);
    if (config_store_.config_is_draft()) {
        finish_config_operation();
        throw ApiError("List refresh is unavailable while a draft config is staged", 409);
    }

    const Config config_snapshot = config_store_.active_config();
    const auto marks_snapshot = allocate_outbound_marks(
        config_snapshot.fwmark.value_or(FwmarkConfig{}),
        config_snapshot.outbounds.value_or(std::vector<Outbound>{}));
    const bool runtime_active_snapshot = runtime_state_store_.snapshot().routing_runtime_active;
    const auto target_selection = select_remote_list_targets(config_snapshot, requested_name);
    if (!target_selection.ok()) {
        finish_config_operation();
        switch (target_selection.error) {
        case RemoteListTargetSelectionError::NotFound:
            throw ApiError("Requested list was not found", 404);
        case RemoteListTargetSelectionError::NotRemote:
            throw ApiError("Requested list is not URL-backed", 400);
        case RemoteListTargetSelectionError::None:
            break;
        }
    }

    try {
        const std::set<std::string> relevant_lists = collect_relevant_list_names(config_snapshot);
        const std::set<std::string> dns_relevant_lists = collect_dns_relevant_list_names(config_snapshot);
        const std::set<std::string> target_lists(target_selection.list_names.begin(),
                                                 target_selection.list_names.end());
        RemoteListsRefreshResult refresh_result = list_service_.refresh_remote_lists(
            config_snapshot,
            marks_snapshot,
            &relevant_lists,
            requested_name ? &target_lists : nullptr,
            &dns_relevant_lists);

        if (!refresh_result.changed_lists.empty()) {
            Logger::instance().info("Lists refresh (api): updated list(s): {}",
                                    format_list_names(refresh_result.changed_lists));
        } else if (!refresh_result.failed_lists.empty()) {
            Logger::instance().warn("Lists refresh (api): failed list(s): {}",
                                    format_list_names(refresh_result.failed_lists));
        } else {
            Logger::instance().info("Lists refresh (api): all checked list(s) are up-to-date.");
        }

        bool reloaded = false;
        bool stale_runtime = false;
        const auto generation = runtime_generation_.load(std::memory_order_acquire);

        enqueue_control_task(
            [this,
             &reloaded,
             &stale_runtime,
             generation,
             runtime_active_snapshot,
             refresh_result]() mutable {
                if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                    stale_runtime = true;
                    Logger::instance().trace("lists_refresh_skip",
                                             "source=api reason=stale_runtime generation={}",
                                             generation);
                    return;
                }

                if (should_reload_runtime_after_list_refresh(runtime_active_snapshot,
                                                            refresh_result)) {
                    reconcile_lists_only(refresh_result.any_dns_relevant_changed());
                    reloaded = true;
                }
            },
            true,
            "api-refresh-lists-commit");

        ListRefreshOperationResult operation_result;
        operation_result.refreshed_lists = std::move(refresh_result.refreshed_lists);
        operation_result.changed_lists = std::move(refresh_result.changed_lists);
        operation_result.failed_lists = std::move(refresh_result.failed_lists);
        operation_result.reloaded = reloaded;

        finish_config_operation();

        if (!target_selection.ok()) {
            return operation_result;
        }
        if (!operation_result.refreshed_lists.size()) {
            operation_result.message = "No URL-backed lists to refresh";
        } else if (!operation_result.failed_lists.empty()) {
            operation_result.message = "Lists refreshed with failures";
        } else if (stale_runtime) {
            operation_result.message =
                "Lists refreshed; runtime changed before reload could be applied";
        } else if (operation_result.changed_lists.empty()) {
            operation_result.message = "Lists refreshed; no updates found";
        } else if (operation_result.reloaded) {
            operation_result.message = "Lists refreshed and runtime reloaded";
        } else if (refresh_result.any_relevant_changed()) {
            operation_result.message =
                "Lists refreshed; runtime is stopped so changes will apply on next start";
        } else {
            operation_result.message = "Lists refreshed";
        }

        return operation_result;
    } catch (...) {
        finish_config_operation();
        throw;
    }
}

void Daemon::setup_api() {
    if (!config_.api || !config_.api->enabled.value_or(false) || opts_.no_api) return;

    api_server_ = std::make_unique<ApiServer>(*config_.api);

    api_ctx_ = std::make_unique<ApiContext>(ApiContext{
        config_path_,
        *dns_test_broadcaster_,
        [this]() {
            return config_store_.visible_config();
        },
        [this]() {
            return config_store_.config_is_draft();
        },
        [this](Config staged_config, std::string staged_config_json) {
            config_store_.stage_config(std::move(staged_config), std::move(staged_config_json));
        },
        [this]() -> std::optional<std::pair<Config, std::string>> {
            return config_store_.staged_snapshot();
        },
        [this]() {
            config_store_.clear_staged();
        },
        [this](const Config& config) {
            validate_config(config);

            const auto active_pid_file = config_store_.active_config()
                .daemon.value_or(DaemonConfig{}).pid_file.value_or("");
            const auto candidate_pid_file = config.daemon.value_or(DaemonConfig{})
                .pid_file.value_or("");
            if (candidate_pid_file != active_pid_file) {
                throw ConfigValidationError(std::vector<ConfigValidationIssue>{{
                    "daemon.pid_file",
                    "daemon.pid_file cannot be changed while the daemon is running",
                }});
            }

            const auto marks = allocate_outbound_marks(
                config.fwmark.value_or(FwmarkConfig{}),
                config.outbounds.value_or(std::vector<Outbound>{}));
            const auto runtime_snapshot = runtime_state_store_.snapshot();
            const auto& urltest_selections = runtime_snapshot.firewall_state.get_urltest_selections();

            (void)build_fw_rule_states(config, marks, &urltest_selections);

            ListStreamer streamer(list_service_.cache_manager());
            const DnsConfig dns_cfg = config.dns.value_or(DnsConfig{});
            DnsServerRegistry dns_registry(dns_cfg);
            const Ipv6SupportDecision ipv6_decision = resolve_ipv6_support(config);
            log_ipv6_support_decision_once(ipv6_decision);
            (void)DnsmasqGenerator::compute_config_hash(
                dns_registry,
                streamer,
                config.route.value_or(RouteConfig{}),
                dns_cfg,
                config.lists.value_or(std::map<std::string, ListConfig>{}),
                KEEN_PBR3_VERSION_FULL_STRING,
                ipv6_decision.enabled);
        },
        [this]() {
            const auto runtime_snapshot = runtime_state_store_.snapshot();
            const auto& system_info = cached_system_info();
            ServiceHealthState service_health;
            service_health.status = runtime_snapshot.routing_runtime_active
                ? api::HealthResponseStatus::RUNNING
                : api::HealthResponseStatus::STOPPED;
            service_health.runtime_state = runtime_state_name(runtime_snapshot.runtime_state);
            service_health.runtime_state_reason = runtime_snapshot.runtime_state_reason;
            service_health.os_type = system_info.os_type;
            service_health.os_version = system_info.os_version;
            service_health.build_variant = system_info.build_variant;
            service_health.resolver_config_hash = runtime_snapshot.resolver_config_hash;
            service_health.resolver_config_hash_actual = runtime_snapshot.resolver_config_hash_actual;
            service_health.resolver_config_hash_actual_ts = runtime_snapshot.resolver_config_hash_actual_ts;
            service_health.resolver_live_status = runtime_snapshot.resolver_live_status;
            service_health.resolver_config_probe_status =
                runtime_snapshot.resolver_config_probe_status;
            service_health.resolver_last_probe_ts = runtime_snapshot.resolver_last_probe_ts;
            service_health.apply_started_ts = runtime_snapshot.apply_started_ts;
            service_health.resolver_config_sync_state =
                runtime_snapshot.resolver_config_sync_state;
            service_health.config_is_draft = config_store_.config_is_draft();
            return service_health;
        },
        [this]() {
            const auto runtime_snapshot = runtime_state_store_.snapshot();

            return build_routing_health_report(
                firewall_->backend(),
                runtime_snapshot.firewall_state,
                runtime_snapshot.route_specs,
                runtime_snapshot.policy_rule_specs,
                netlink_);
        },
        [this]() {
            const Config config_snapshot = config_store_.active_config();
            const auto runtime_snapshot = runtime_state_store_.snapshot();

            return build_runtime_outbounds_response(
                config_snapshot,
                netlink_,
                [&runtime_snapshot](const std::string& tag) -> std::optional<UrltestState> {
                    auto it = runtime_snapshot.urltest_states.find(tag);
                    if (it == runtime_snapshot.urltest_states.end()) {
                        return std::nullopt;
                    }
                    return it->second;
                });
        },
        [this]() {
            auto response = build_runtime_interface_inventory_response_or_empty(netlink_);
            populate_keenetic_interface_descriptions(response);
            return response;
        },
        [this](const Config& config) {
            return build_list_refresh_state_map(config, list_service_.cache_manager());
        },
        [this](const std::string& target) {
            const Config visible_config = config_store_.visible_config();
            return compute_test_routing(visible_config, list_service_.cache_manager(), target);
        },
        [this]() {
            begin_config_operation_or_throw(ConfigOperationState::Saving,
                                            "begin-save",
                                            false,
                                            false);
        },
        [this]() {
            finish_config_operation();
        },
        [this](Config config, std::string saved_config_json) -> ConfigApplyResult {
            return apply_validated_config_via_control_task(std::move(config),
                                                           std::move(saved_config_json));
        },
        [this]() {
            begin_config_operation_or_throw(ConfigOperationState::Reloading,
                                            "start-runtime",
                                            false,
                                            true);
            run_runtime_control_operation_or_throw("api-start-runtime",
                                                   "Start routing runtime",
                                                   [this]() { start_routing_runtime(); });
        },
        [this]() {
            begin_config_operation_or_throw(ConfigOperationState::Reloading,
                                            "stop-runtime",
                                            true,
                                            false);
            run_runtime_control_operation_or_throw("api-stop-runtime",
                                                   "Stop routing runtime",
                                                   [this]() { stop_routing_runtime(); });
        },
        [this]() {
            begin_config_operation_or_throw(ConfigOperationState::Reloading,
                                            "restart-runtime",
                                            true,
                                            false);
            run_runtime_control_operation_or_throw("api-restart-runtime",
                                                   "Restart routing runtime",
                                                   [this]() { restart_routing_runtime(); });
        },
        [this](std::optional<std::string> requested_name) {
            return refresh_lists_via_api(requested_name);
        },
    });
    register_api_handlers(*api_server_, *api_ctx_);

    const std::filesystem::path frontend_root(KEEN_PBR_FRONTEND_ROOT);
    const std::filesystem::path frontend_index = frontend_root / "index.html";
    std::filesystem::path frontend_index_gzip = frontend_index;
    frontend_index_gzip += ".gz";
    const bool has_frontend_root =
        std::filesystem::is_directory(frontend_root) &&
        (std::filesystem::is_regular_file(frontend_index) ||
         std::filesystem::is_regular_file(frontend_index_gzip));
    if (!has_frontend_root) {
        Logger::instance().warn(
            "API enabled but frontend root is unavailable: {} (missing directory or index.html(.gz)). API endpoints will remain available.",
            frontend_root.string());
    } else if (!api_server_->register_static_root(frontend_root.string())) {
        Logger::instance().warn(
            "Failed to register frontend static root: {}. API endpoints will remain available.",
            frontend_root.string());
    } else {
        Logger::instance().info("Frontend static root: {}", frontend_root.string());
    }

    const std::string listen_addr = config_.api->listen.value_or("0.0.0.0:12121");
    Logger::instance().info("Starting REST API on {}", listen_addr);
    try {
        api_server_->start();
        Logger::instance().info("REST API listening on {}", listen_addr);
    } catch (const ApiError& e) {
        Logger::instance().error("REST API startup failed on {}: {}", listen_addr, e.what());
        throw;
    } catch (const std::exception& e) {
        Logger::instance().error("Unexpected REST API startup failure on {}: {}",
                                 listen_addr,
                                 e.what());
        throw;
    }
}

} // namespace keen_pbr3

#endif
