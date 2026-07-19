#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Backend-neutral desired state. Concrete reconcilers interpret the stable
// identities in these collections; this keeps the transaction coordinator
// independent from iptables, nftables, and netlink representation details.
struct RoutingDesiredState {
    std::vector<std::string> rules;
    std::vector<std::string> routes;
};

struct FirewallDesiredState {
    std::vector<std::string> chains;
    std::vector<std::string> rules;
    std::vector<std::string> sets;
};

struct ResolverDesiredState {
    std::string configuration_id;
};

struct ConntrackDesiredState {
    std::string policy_id;
};

struct RuntimeDesiredState {
    RoutingDesiredState routing;
    FirewallDesiredState firewall;
    ResolverDesiredState resolver;
    ConntrackDesiredState conntrack;
};

// Actual state is only obtained through RuntimeSubsystem::inspect(). It uses
// the same neutral shape as desired state so a backend can verify independently
// after applying a plan.
struct RuntimeActualState {
    RoutingDesiredState routing;
    FirewallDesiredState firewall;
    ResolverDesiredState resolver;
    ConntrackDesiredState conntrack;
};

class RuntimeOperation {
public:
    virtual ~RuntimeOperation() = default;
    virtual void apply() const = 0;
};

using RuntimeOperationPtr = std::shared_ptr<const RuntimeOperation>;

enum class RuntimeComponent {
    routing,
    firewall,
    resolver,
    conntrack,
};

class RuntimeSubsystem {
public:
    virtual ~RuntimeSubsystem() = default;

    virtual RuntimeComponent component() const = 0;
    virtual RuntimeActualState inspect() const = 0;
    virtual std::vector<RuntimeOperationPtr> plan(const RuntimeDesiredState& desired,
                                                   const RuntimeActualState& actual) const = 0;
    virtual bool verify(const RuntimeDesiredState& desired,
                        const RuntimeActualState& actual,
                        std::string& error) const = 0;
};

struct RuntimeReconcileResult {
    bool committed{false};
    bool drift_detected{false};
    std::string error;
    size_t operation_count{0};
};

// Coordinates inspect -> plan -> apply -> independent inspect/verify -> commit.
// Plans are local values, deliberately discarded on every return path.
class RuntimeReconciler {
public:
    explicit RuntimeReconciler(std::vector<std::reference_wrapper<RuntimeSubsystem>> subsystems);

    RuntimeReconcileResult reconcile(const RuntimeDesiredState& desired,
                                     const std::function<void()>& commit = {});

    // Read-only diagnostic path. It must never invoke plan(), apply(), or commit().
    RuntimeActualState inspect() const;

private:
    std::vector<std::reference_wrapper<RuntimeSubsystem>> subsystems_;
};

} // namespace keen_pbr3
