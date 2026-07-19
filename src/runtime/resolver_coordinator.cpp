#include "resolver_coordinator.hpp"

#include <utility>

namespace keen_pbr3 {

bool ResolverCoordinator::reconcile(std::string expected_config_hash) {
    if (metadata_.expected_config_hash == expected_config_hash) {
        return false;
    }
    metadata_.expected_config_hash = std::move(expected_config_hash);
    return true;
}

void ResolverCoordinator::observe_actual(std::string actual_config_hash) {
    metadata_.actual_config_hash = std::move(actual_config_hash);
}

void ResolverCoordinator::clear_actual() {
    metadata_.actual_config_hash.clear();
}

ResolverMetadata ResolverCoordinator::inspect() const {
    return metadata_;
}

} // namespace keen_pbr3
