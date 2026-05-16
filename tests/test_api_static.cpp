#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>

#include "../src/api/server.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

std::filesystem::path make_temp_static_root() {
    const auto base = std::filesystem::temp_directory_path();
    for (int i = 0; i < 100; ++i) {
        auto path = base / ("keen-pbr-static-test-" + std::to_string(::getpid()) + "-" + std::to_string(i));
        std::error_code ec;
        if (std::filesystem::create_directory(path, ec)) {
            return path;
        }
    }
    throw std::runtime_error("failed to create temporary static root");
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open test file: " + path.string());
    }
    file << content;
}

} // namespace

TEST_CASE("static frontend gzip response does not duplicate compression headers") {
    const auto root = make_temp_static_root();
    write_file(root / "index.html", "<!doctype html><title>keen-pbr</title>");
    write_file(root / "index.html.gz", "pretend-gzip-bytes");

    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18190");

    ApiServer server(api_config);
    REQUIRE(server.register_static_root(root.string()));
    server.start();

    httplib::Client client("127.0.0.1", 18190);
    client.set_decompress(false);
    const httplib::Headers headers{{"Accept-Encoding", "gzip"}};
    const auto response = client.Get("/", headers);
    server.stop();

    std::filesystem::remove_all(root);

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(response->get_header_value_count("Content-Encoding") == 1);
    CHECK(response->get_header_value("Content-Encoding") == "gzip");
    CHECK(response->get_header_value_count("Vary") == 1);
    CHECK(response->get_header_value("Vary") == "Accept-Encoding");
    CHECK(response->get_header_value("Content-Length") == "18");
    CHECK(response->body == "pretend-gzip-bytes");
}

TEST_CASE("static frontend gzip variant is skipped without gzip accept header") {
    const auto root = make_temp_static_root();
    write_file(root / "index.html", "<!doctype html><title>keen-pbr</title>");
    write_file(root / "index.html.gz", "pretend-gzip-bytes");

    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18191");

    ApiServer server(api_config);
    REQUIRE(server.register_static_root(root.string()));
    server.start();

    httplib::Client client("127.0.0.1", 18191);
    client.set_decompress(false);
    const httplib::Headers headers{{"Accept-Encoding", "identity"}};
    const auto response = client.Get("/", headers);
    server.stop();

    std::filesystem::remove_all(root);

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(response->get_header_value_count("Content-Encoding") == 0);
    CHECK(response->get_header_value_count("Vary") == 0);
    CHECK(response->body == "<!doctype html><title>keen-pbr</title>");
}

TEST_CASE("static frontend gzip variant respects explicit q zero") {
    const auto root = make_temp_static_root();
    write_file(root / "index.html", "<!doctype html><title>keen-pbr</title>");
    write_file(root / "index.html.gz", "pretend-gzip-bytes");

    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18192");

    ApiServer server(api_config);
    REQUIRE(server.register_static_root(root.string()));
    server.start();

    httplib::Client client("127.0.0.1", 18192);
    client.set_decompress(false);
    const httplib::Headers headers{{"Accept-Encoding", "gzip;q=0, *;q=1"}};
    const auto response = client.Get("/", headers);
    server.stop();

    std::filesystem::remove_all(root);

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(response->get_header_value_count("Content-Encoding") == 0);
    CHECK(response->body == "<!doctype html><title>keen-pbr</title>");
}

} // namespace keen_pbr3

#endif // WITH_API
