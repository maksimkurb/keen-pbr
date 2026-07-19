#include <doctest/doctest.h>

#include "../src/ipc/control_protocol.hpp"
#include "../src/ipc/control_client.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <thread>

using namespace keen_pbr3::ipc;

TEST_CASE("control protocol round-trips a versioned request envelope") {
    const nlohmann::json request{{"protocol_version", kControlProtocolVersion},
                                 {"request_id", "request-1"},
                                 {"operation", "status"}};

    const auto decoded = decode_message(encode_message(request));
    CHECK(decoded == request);
    CHECK_NOTHROW(validate_request_envelope(decoded));
}

TEST_CASE("control protocol rejects malformed and incompatible envelopes") {
    CHECK_THROWS_AS(decode_message(std::string("\0\0\0", 3)), ControlProtocolError);

    auto invalid = nlohmann::json{{"protocol_version", 999},
                                  {"request_id", "request-1"},
                                  {"operation", "status"}};
    CHECK_THROWS_AS(validate_request_envelope(invalid), ControlProtocolError);
}

TEST_CASE("control protocol error response preserves request correlation") {
    const auto response = make_error_response(
        nlohmann::json{{"request_id", "request-1"}}, "version_mismatch", "unsupported");
    CHECK(response["request_id"] == "request-1");
    CHECK_FALSE(response["ok"]);
    CHECK(response["error"]["code"] == "version_mismatch");
}

TEST_CASE("control client exchanges one framed request with a Unix daemon socket") {
    const auto path = "/tmp/keen-pbr-control-client-" + std::to_string(getpid()) + ".sock";
    (void)unlink(path.c_str());
    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(listener >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1U);
    REQUIRE(bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(listen(listener, 1) == 0);

    std::atomic<bool> served{false};
    std::thread server([&] {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) return;
        std::uint32_t length = 0;
        if (recv(client, &length, sizeof(length), MSG_WAITALL) != sizeof(length)) {
            close(client);
            return;
        }
        const auto payload_size = static_cast<std::size_t>(ntohl(length));
        std::string frame(sizeof(length) + payload_size, '\0');
        std::memcpy(frame.data(), &length, sizeof(length));
        if (recv(client, frame.data() + sizeof(length), payload_size, MSG_WAITALL) !=
            static_cast<ssize_t>(payload_size)) {
            close(client);
            return;
        }
        const auto request = decode_message(frame);
        const auto response = encode_message({{"protocol_version", kControlProtocolVersion},
                                              {"request_id", request.at("request_id")},
                                              {"ok", true},
                                              {"result", {{"value", "active"}}}});
        served.store(send(client, response.data(), response.size(), MSG_NOSIGNAL) ==
                     static_cast<ssize_t>(response.size()));
        close(client);
    });

    nlohmann::json response;
    CHECK_NOTHROW(response = request_control(
        path,
        {{"protocol_version", kControlProtocolVersion},
         {"request_id", "client-1"},
         {"operation", "status"}},
        1000));
    server.join();
    CHECK(served.load());
    CHECK(response.at("result").at("value") == "active");
    close(listener);
    (void)unlink(path.c_str());
}

TEST_CASE("control client streams bounded resolver chunks without buffering the response") {
    const auto path = "/tmp/keen-pbr-control-stream-" + std::to_string(getpid()) + ".sock";
    (void)unlink(path.c_str());
    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(listener >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1U);
    REQUIRE(bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(listen(listener, 1) == 0);

    std::thread server([&] {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) return;
        std::uint32_t request_size = 0;
        if (recv(client, &request_size, sizeof(request_size), MSG_WAITALL) != sizeof(request_size)) return;
        std::string discard(ntohl(request_size), '\0');
        (void)recv(client, discard.data(), discard.size(), MSG_WAITALL);
        const auto header = encode_message({{"protocol_version", kControlProtocolVersion},
                                            {"request_id", "stream-1"}, {"ok", true}, {"stream", true}});
        (void)send(client, header.data(), header.size(), MSG_NOSIGNAL);
        for (const std::string chunk : {std::string("alpha-"), std::string("beta")}) {
            const std::uint32_t size = htonl(static_cast<std::uint32_t>(chunk.size()));
            (void)send(client, &size, sizeof(size), MSG_NOSIGNAL);
            (void)send(client, chunk.data(), chunk.size(), MSG_NOSIGNAL);
        }
        const std::uint32_t end = 0;
        (void)send(client, &end, sizeof(end), MSG_NOSIGNAL);
        close(client);
    });

    std::ostringstream output;
    CHECK_NOTHROW(stream_control(path,
                                 {{"protocol_version", kControlProtocolVersion},
                                  {"request_id", "stream-1"}, {"operation", "generate-resolver-config"}},
                                 output, 1000));
    server.join();
    CHECK(output.str() == "alpha-beta");
    close(listener);
    (void)unlink(path.c_str());
}

TEST_CASE("control client reports a truncated resolver stream after active bytes") {
    const auto path = "/tmp/keen-pbr-control-truncated-" + std::to_string(getpid()) + ".sock";
    (void)unlink(path.c_str());
    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(listener >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1U);
    REQUIRE(bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(listen(listener, 1) == 0);

    std::thread server([&] {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) return;
        std::uint32_t request_size = 0;
        if (recv(client, &request_size, sizeof(request_size), MSG_WAITALL) != sizeof(request_size)) return;
        std::string discard(ntohl(request_size), '\0');
        (void)recv(client, discard.data(), discard.size(), MSG_WAITALL);
        const auto header = encode_message({{"protocol_version", kControlProtocolVersion},
                                            {"request_id", "stream-2"}, {"ok", true}, {"stream", true}});
        (void)send(client, header.data(), header.size(), MSG_NOSIGNAL);
        const std::string chunk = "active";
        const std::uint32_t size = htonl(static_cast<std::uint32_t>(chunk.size()));
        (void)send(client, &size, sizeof(size), MSG_NOSIGNAL);
        (void)send(client, chunk.data(), chunk.size(), MSG_NOSIGNAL);
        close(client);
    });

    std::ostringstream output;
    try {
        stream_control(path,
                       {{"protocol_version", kControlProtocolVersion},
                        {"request_id", "stream-2"}, {"operation", "generate-resolver-config"}},
                       output, 1000);
        FAIL("expected stream failure");
    } catch (const ControlStreamError& error) {
        CHECK(error.active_bytes_streamed());
    }
    server.join();
    CHECK(output.str() == "active");
    close(listener);
    (void)unlink(path.c_str());
}
