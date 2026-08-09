#ifdef WITH_API

#include "handler_dns_test.hpp"
#include "sse_response.hpp"

#include "../log/logger.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

std::string make_sse_frame(const std::string& payload) {
    return "data: " + payload + "\n\n";
}

std::string make_hello_payload() {
    nlohmann::json payload = {
        {"type", "HELLO"}
    };
    return payload.dump();
}

} // namespace

void register_dns_test_handler(ApiServer& server, ApiContext& ctx) {
    server.get_stream("/api/dns/test",
                      [&ctx](const httplib::Request&, httplib::Response& res) {
        auto subscription = ctx.dns_test_broadcaster.subscribe({make_hello_payload()});
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
