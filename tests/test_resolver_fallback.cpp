#include <doctest/doctest.h>

#include "../src/ipc/resolver_fallback.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

using namespace keen_pbr3::ipc;

TEST_CASE("resolver fallback streams static content with a safe marker") {
    const std::string path = "/tmp/keen-pbr-fallback-" + std::to_string(getpid());
    { std::ofstream file(path); file << "server=/example.test/192.0.2.1\n"; }
    std::ostringstream output;
    REQUIRE(emit_resolver_fallback(output, path, "socket_unavailable", 123));
    CHECK(output.str().find("fallback reason=socket_unavailable") != std::string::npos);
    CHECK(output.str().find("config-hash.keen.pbr,123|") != std::string::npos);
    CHECK(output.str().find("123|fallback|socket_unavailable") != std::string::npos);
    CHECK(output.str().find("server=/example.test/192.0.2.1") != std::string::npos);
    std::remove(path.c_str());
}

TEST_CASE("resolver fallback rejects unsafe reason codes and missing files") {
    std::ostringstream output;
    CHECK_FALSE(emit_resolver_fallback(output, "/missing", "not valid", 1));
    CHECK_FALSE(emit_resolver_fallback(output, "/missing", "socket_unavailable", 1));
}
