#pragma once

#include <string>

namespace keen_pbr3 {

struct ResolverMetadata {
    std::string expected_config_hash;
    std::string actual_config_hash;
};

class ResolverCoordinator {
public:
    bool reconcile(std::string expected_config_hash);
    void observe_actual(std::string actual_config_hash);
    void clear_actual();
    ResolverMetadata inspect() const;

private:
    ResolverMetadata metadata_;
};

} // namespace keen_pbr3
