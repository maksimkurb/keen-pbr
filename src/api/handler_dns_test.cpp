#ifdef WITH_API

#include "handler_dns_test.hpp"
#include "sse_response.hpp"

#include "../log/logger.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <optional>

namespace keen_pbr3 {

namespace {

struct DnsTestView {
    SseBroadcaster::MessageFilter filter;
    bool close_after_match{false};
};

std::string normalize_marker_domain(std::string value) {
    while (!value.empty() && value.back() == '.') value.pop_back();
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string make_sse_frame(const std::string& payload) {
    return "data: " + payload + "\n\n";
}

std::string make_hello_payload() {
    nlohmann::json payload = {
        {"type", "HELLO"}
    };
    return payload.dump();
}

DnsTestView view_for(const httplib::Request& request) {
    std::optional<std::string> marker_domain;
    if (request.has_param("domain")) {
        marker_domain = normalize_marker_domain(request.get_param_value("domain"));
        if (marker_domain->empty()) {
            throw ApiError("Invalid query parameter 'domain'; expected a non-empty domain", 400);
        }
    }

    const auto marker_filter = [marker_domain](const std::string&, const SseMessageMeta& meta) {
        if (meta.source != "marker") return false;
        if (!marker_domain) return true;
        return normalize_marker_domain(meta.domain) == *marker_domain;
    };
    if (!request.has_param("show")) {
        // The dashboard/check view is intentionally the safe default: it does
        // not retain an overview monitor's high-volume stream.
        return {marker_filter, true};
    }

    const auto show = request.get_param_value("show");
    if (show == "keen-pbr") {
        return {marker_filter, true};
    }
    if (show == "all" || show == "full") {
        return {};
    }

    throw ApiError(
        "Invalid query parameter 'show'; expected 'keen-pbr' (default), 'all', or 'full'",
        400);
}

} // namespace

void register_dns_test_handler(ApiServer& server, ApiContext& ctx) {
    server.get_stream("/api/dns/test",
                      [&ctx](const httplib::Request& request, httplib::Response& res) {
        const auto view = view_for(request);
        auto subscription = ctx.dns_test_broadcaster.subscribe(
            {make_hello_payload()}, view.filter, view.close_after_match);
        Logger::instance().trace("sse_open", "path=/api/dns/test");
        set_sse_response(
            res,
            subscription,
            [&ctx, subscription] {
                Logger::instance().trace("sse_close", "path=/api/dns/test");
                ctx.dns_test_broadcaster.unsubscribe(subscription);
            },
            [](std::string message) {
                const auto frame = make_sse_frame(message);
                Logger::instance().trace("sse_event", "path=/api/dns/test bytes={}", frame.size());
                return frame;
            });
    });
}

} // namespace keen_pbr3

#endif // WITH_API
