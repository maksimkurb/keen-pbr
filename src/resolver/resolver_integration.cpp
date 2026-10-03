#include "resolver_integration.hpp"

#include "dnsmasq_integration.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../log/logger.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/safe_exec.hpp"

#include <keen-pbr/version.hpp>

#include <stdexcept>

namespace keen_pbr3 {

int default_hook_command_executor(const std::vector<std::string>& args) {
    return safe_exec(args);
}

ResolverHealthReport NoResolverIntegration::health() const {
    ResolverHealthReport report;
    report.mode = api::ResolverIntegration::NONE;
    report.probe_status = api::ResolverConfigProbeStatus::DISABLED;
    return report;
}

std::unique_ptr<ResolverIntegration> make_resolver_integration(
    api::ResolverIntegration mode, const ResolverIntegrationDeps& deps) {
    if (mode == api::ResolverIntegration::DNSMASQ) {
        return std::make_unique<DnsmasqIntegration>(deps.host, deps.hook_executor);
    }
    return std::make_unique<NoResolverIntegration>();
}

bool reconfigure_resolver_integration(std::unique_ptr<ResolverIntegration>& current,
                                      const Config& config,
                                      const ResolverIntegrationDeps& deps,
                                      const ResolverIntegrationFactory& factory) {
    const api::ResolverIntegration next_mode = effective_resolver_integration(config);
    if (current && current->mode() == next_mode) {
        // `configure` may make DnsmasqIntegration::active() false.  Remove an
        // already-installed hook first, while the old resolver settings are
        // still available; otherwise a same-mode config edit can strand the
        // hook with no way for the new settings to clean it up.
        const bool next_has_resolver =
            config.dns.has_value() && config.dns->system_resolver.has_value() &&
            !config.dns->system_resolver->address.empty();
        if (current->enabled() && current->active() && !next_has_resolver) {
            current->runtime_stopping();
            if (!current->deactivate()) {
                throw std::runtime_error(
                    "resolver integration deactivation failed while removing "
                    "the system resolver");
            }
            current->drain_callbacks(std::chrono::milliseconds{100});
        }
        current->configure(config);
        return false;
    }

    // Construct and configure the replacement first.  A factory/configure
    // failure must not tear down the currently owned resolver hook.
    auto replacement = factory(next_mode, deps);
    replacement->configure(config);

    if (current) {
        if (current->enabled()) {
            // Leaving the old integration: hand the resolver back its static
            // fallback configuration before the instance goes away.
            Logger::instance().info("Resolver integration changes from {} to {}",
                                    resolver_integration_name(current->mode()),
                                    resolver_integration_name(next_mode));
            current->runtime_stopping();
            bool deactivated = false;
            try {
                deactivated = current->deactivate();
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    "resolver integration deactivation failed while switching to " +
                    std::string(resolver_integration_name(next_mode)) + ": " + error.what());
            }
            if (!deactivated) {
                // Keep the old object and its ownership state intact.  The
                // caller must not publish a new mode while the old hook may
                // still be installed in the system resolver.
                throw std::runtime_error(
                    "resolver integration deactivation failed while switching to " +
                    std::string(resolver_integration_name(next_mode)));
            }
            current->drain_callbacks(std::chrono::milliseconds{100});
        }
        current->shutdown();
    }

    current = std::move(replacement);
    return true;
}

void dry_run_resolver_generation(const Config& config,
                                 ListStreamer& streamer,
                                 bool ipv6_enabled) {
    if (effective_resolver_integration(config) != api::ResolverIntegration::DNSMASQ) {
        return;
    }
    const DnsConfig dns_cfg = config.dns.value_or(DnsConfig{});
    DnsServerRegistry dns_registry(dns_cfg);
    (void)DnsmasqGenerator::compute_config_hash(
        dns_registry,
        streamer,
        config.route.value_or(RouteConfig{}),
        dns_cfg,
        config.lists.value_or(std::map<std::string, ListConfig>{}),
        KEEN_PBR3_VERSION_FULL_STRING,
        ipv6_enabled);
}

} // namespace keen_pbr3
