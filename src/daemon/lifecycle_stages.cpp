#include "lifecycle_stages.hpp"

namespace keen_pbr3 {

std::vector<LifecycleOperationStage> lifecycle_stages(LifecycleOperationType type,
                                                      bool resolver_integration) {
    const LifecycleOperationStage reload{"reload_dnsmasq", "Reload dnsmasq"};
    const LifecycleOperationStage verify{"verify_dnsmasq", "Verify dnsmasq configuration"};
    std::vector<LifecycleOperationStage> stages;
    switch (type) {
    case LifecycleOperationType::ApplyConfig:
        stages = {{"validate_config", "Validate configuration"},
                  {"prepare_remote_lists", "Prepare remote lists"},
                  {"commit_config", "Commit configuration"},
                  {"reconcile_runtime", "Reconcile routing and firewall"}};
        if (resolver_integration) stages.insert(stages.end(), {reload, verify});
        break;
    case LifecycleOperationType::RollbackConfig:
        stages = {{"restore_config", "Restore previous configuration"},
                  {"validate_config", "Validate restored configuration"},
                  {"prepare_remote_lists", "Prepare remote lists"},
                  {"reconcile_runtime", "Reconcile routing and firewall"}};
        if (resolver_integration) stages.insert(stages.end(), {reload, verify});
        break;
    case LifecycleOperationType::Restart:
        stages = {{"stop_routing", "Stop routing and firewall"},
                  {"start_routing", "Start routing and firewall"}};
        if (resolver_integration) stages.insert(stages.end(), {reload, verify});
        break;
    case LifecycleOperationType::Start:
        stages = {{"start_routing", "Start routing and firewall"}};
        if (resolver_integration) stages.insert(stages.end(), {reload, verify});
        break;
    case LifecycleOperationType::Stop:
        stages = {{"stop_routing", "Stop routing and firewall"}};
        if (resolver_integration) {
            stages.push_back({"reload_fallback", "Reload dnsmasq with fallback configuration"});
        }
        break;
    }
    return stages;
}

} // namespace keen_pbr3
