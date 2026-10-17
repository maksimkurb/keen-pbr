#include <doctest/doctest.h>

#include "../src/cmd/resolver_config.hpp"
#include "../src/dns/dnsmasq_gen.hpp"

#include <keen-pbr/version.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace keen_pbr3;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("keen-pbr-test-resolver-config-" + std::to_string(::getpid()));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::string write(const std::string& name, const std::string& text) const {
        const auto file = path / name;
        std::ofstream(file) << text;
        return file.string();
    }
};

const std::string kFallbackFile =
    "# comment\nserver=9.9.9.9\n  server=9.9.8.8\n# server=1.2.3.4\nno-resolv\n";

std::string config_text(const std::string& dir, const std::string& integration,
                        bool with_fallback) {
    std::string json = std::string("{\"version\":\"") + KEEN_PBR3_VERSION_STRING +
        "\",\"daemon\":{\"cache_dir\":\"" + dir + "/cache\"},"
        "\"lists\":{\"l\":{\"domains\":[\"example.com\"]}},"
        "\"dns\":{\"servers\":[{\"tag\":\"up\",\"address\":\"1.1.1.1\"}],"
        "\"rules\":[{\"list\":[\"l\"],\"servers\":[\"up\"]}]";
    if (!integration.empty()) json += ",\"resolver_integration\":\"" + integration + "\"";
    if (with_fallback) json += ",\"fallback\":[\"up\"]";
    return json + "}}";
}

std::string run(const std::string& type, const std::string& config,
                const std::string& fallback) {
    std::ostringstream out;
    CHECK(run_generate_resolver_config(type, config, fallback, out) == 0);
    return out.str();
}

bool has_line(const std::string& text, const std::string& line) {
    return text.find("\n" + line + "\n") != std::string::npos ||
           text.rfind(line + "\n", 0) == 0;
}

std::string stamp_hash(const std::string& text) {
    const std::string key = "txt-record=config-hash.keen.pbr,";
    const auto pos = text.rfind(key);
    REQUIRE(pos != std::string::npos);
    return text.substr(pos + key.size(), 32);
}

}  // namespace

TEST_CASE("generate-resolver-config: integration none prints only the fallback servers") {
    TempDir dir;
    const auto fallback = dir.write("fallback.conf", kFallbackFile);
    const auto config = dir.write("config.json", config_text(dir.path.string(), "none", true));
    const auto out = run("dnsmasq", config, fallback);
    CHECK(has_line(out, "server=9.9.9.9"));
    CHECK(has_line(out, "server=9.9.8.8"));
    CHECK_FALSE(has_line(out, "no-resolv"));
    CHECK(out.find("1.1.1.1") == std::string::npos);
    CHECK(out.find("server=/example.com/") == std::string::npos);
    CHECK(stamp_hash(out) == kDnsmasqOffStampHash);
}

TEST_CASE("generate-resolver-config: dnsmasq with default servers keeps the fallback file out") {
    TempDir dir;
    const auto fallback = dir.write("fallback.conf", kFallbackFile);
    const auto config = dir.write("config.json", config_text(dir.path.string(), "dnsmasq", true));
    const auto out = run("dnsmasq", config, fallback);
    CHECK(has_line(out, "no-resolv"));
    CHECK(has_line(out, "server=1.1.1.1"));
    CHECK(out.find("server=/example.com/1.1.1.1") != std::string::npos);
    CHECK(out.find("9.9.9.9") == std::string::npos);
    CHECK(stamp_hash(out) != kDnsmasqOffStampHash);
}

TEST_CASE("generate-resolver-config: dnsmasq without default servers appends the fallback servers") {
    TempDir dir;
    const auto fallback = dir.write("fallback.conf", kFallbackFile);
    const auto config = dir.write("config.json", config_text(dir.path.string(), "dnsmasq", false));
    const auto out = run("dnsmasq", config, fallback);
    CHECK_FALSE(has_line(out, "no-resolv"));
    CHECK(out.find("server=/example.com/1.1.1.1") != std::string::npos);
    CHECK(has_line(out, "server=9.9.9.9"));
    CHECK(has_line(out, "server=9.9.8.8"));
    // The fallback lines come after the generated part and before the stamp.
    CHECK(out.find("server=9.9.9.9") > out.find("server=/example.com/"));
    CHECK(out.find("server=9.9.9.9") < out.find("txt-record=config-hash.keen.pbr"));
    CHECK(stamp_hash(out) != kDnsmasqOffStampHash);
}

TEST_CASE("generate-resolver-config: the managed hash does not depend on the fallback file") {
    TempDir dir;
    const auto fallback_a = dir.write("a.conf", kFallbackFile);
    const auto fallback_b = dir.write("b.conf", "server=4.4.4.4\n");
    const auto config = dir.write("config.json", config_text(dir.path.string(), "dnsmasq", false));
    CHECK(stamp_hash(run("dnsmasq", config, fallback_a)) ==
          stamp_hash(run("dnsmasq", config, fallback_b)));
}

TEST_CASE("generate-resolver-config: unreadable or invalid config prints the fallback servers") {
    TempDir dir;
    const auto fallback = dir.write("fallback.conf", kFallbackFile);
    const auto missing = run("dnsmasq", (dir.path / "nope.json").string(), fallback);
    CHECK(missing.find("# keen-pbr: generate-resolver-config failed") != std::string::npos);
    CHECK(has_line(missing, "server=9.9.9.9"));
    CHECK(stamp_hash(missing) == kDnsmasqOffStampHash);

    const auto broken = dir.write("broken.json", "{not json");
    const auto out = run("dnsmasq", broken, fallback);
    CHECK(has_line(out, "server=9.9.9.9"));
    CHECK(stamp_hash(out) == kDnsmasqOffStampHash);

    // A missing fallback file prints no servers but still succeeds.
    const auto bare = run("dnsmasq", broken, (dir.path / "none.conf").string());
    CHECK(bare.find("server=") == std::string::npos);
    CHECK(stamp_hash(bare) == kDnsmasqOffStampHash);
}

TEST_CASE("generate-resolver-config: unknown resolver type is a comment") {
    TempDir dir;
    const auto out = run("unbound", "/nonexistent", "/nonexistent");
    CHECK(out == "# keen-pbr: unsupported resolver type\n");
}
