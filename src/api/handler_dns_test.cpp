#ifdef WITH_API

#include "handler_dns_test.hpp"

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
        auto subscription = ctx.dns_test_broadcaster.subscribe();
        Logger::instance().trace("sse_open", "path=/api/dns/test");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider(
            "text/event-stream",
            [subscription, hello_sent = false](size_t, httplib::DataSink& sink) mutable -> bool {
                if (!hello_sent) {
                    hello_sent = true;
                    const auto hello = make_sse_frame(make_hello_payload());
                    Logger::instance().trace("sse_hello", "path=/api/dns/test bytes={}", hello.size());
                    if (!sink.write(hello.data(), hello.size())) {
                        return false;
                    }
                    return true;
                }

                std::string next_message;
                {
                    KPBR_UNIQUE_LOCK(lock, subscription->mutex);
                    subscription->cv.wait(lock, [&]() {
                        return subscription->closed || !subscription->messages.empty();
                    });

                    if (subscription->messages.empty()) {
                        Logger::instance().trace("sse_done", "path=/api/dns/test");
                        sink.done();
                        return true;
                    }

                    next_message = std::move(subscription->messages.front());
                    subscription->messages.pop_front();
                }

                const auto frame = make_sse_frame(next_message);
                Logger::instance().trace("sse_event", "path=/api/dns/test bytes={}", frame.size());
                return sink.write(frame.data(), frame.size());
            },
            [&ctx, subscription](bool) {
                Logger::instance().trace("sse_close", "path=/api/dns/test");
                ctx.dns_test_broadcaster.unsubscribe(subscription);
            });
    });
}

} // namespace keen_pbr3

#endif // WITH_API
