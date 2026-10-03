#include <doctest/doctest.h>

#include "../src/lists/domain_index.hpp"

#include <vector>

namespace keen_pbr3 {

TEST_CASE("DomainIndex: exact match and subdomain match") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    builder.add_domain(list_a, "example.com");

    auto index = std::move(builder).build();

    std::vector<DomainIndex::ListId> result;
    index.lookup("example.com", result);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == list_a);

    result.clear();
    index.lookup("a.b.example.com", result);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == list_a);
}

TEST_CASE("DomainIndex: label boundary") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    builder.add_domain(list_a, "example.com");

    auto index = std::move(builder).build();

    std::vector<DomainIndex::ListId> result;
    index.lookup("badexample.com", result);
    CHECK(result.empty());

    result.clear();
    index.lookup("com", result);
    CHECK(result.empty());
}

TEST_CASE("DomainIndex: case and trailing dot handling") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    builder.add_domain(list_a, "Example.COM");

    auto index = std::move(builder).build();

    std::vector<DomainIndex::ListId> result;
    index.lookup("WWW.example.com.", result);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == list_a);
}

TEST_CASE("DomainIndex: wildcard entry normalized") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    builder.add_domain(list_a, "*.foo.org");

    auto index = std::move(builder).build();

    std::vector<DomainIndex::ListId> result;
    // "*.foo.org" should be normalized to "foo.org"
    index.lookup("bar.foo.org", result);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == list_a);

    result.clear();
    index.lookup("foo.org", result);
    REQUIRE(result.size() == 1);
    CHECK(result[0] == list_a);
}

TEST_CASE("DomainIndex: multiple lists") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    auto list_b = builder.add_list("B");
    auto list_c = builder.add_list("C");

    builder.add_domain(list_a, "x.com");
    builder.add_domain(list_b, "x.com");
    builder.add_domain(list_c, "y.x.com");

    auto index = std::move(builder).build();

    std::vector<DomainIndex::ListId> result;
    index.lookup("z.y.x.com", result);
    REQUIRE(result.size() == 3);
    CHECK(result[0] == list_a);
    CHECK(result[1] == list_b);
    CHECK(result[2] == list_c);

    result.clear();
    index.lookup("x.com", result);
    REQUIRE(result.size() == 2);
    CHECK(result[0] == list_a);
    CHECK(result[1] == list_b);
}

TEST_CASE("DomainIndex: duplicate domains") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    builder.add_domain(list_a, "example.com");
    builder.add_domain(list_a, "example.com");

    auto index = std::move(builder).build();

    CHECK(index.domain_count() == 1);

    std::vector<DomainIndex::ListId> result;
    index.lookup("example.com", result);
    REQUIRE(result.size() == 1);
}

TEST_CASE("DomainIndex: empty index") {
    DomainIndex::Builder builder;
    auto index = std::move(builder).build();

    CHECK(index.empty());
    CHECK(index.domain_count() == 0);

    std::vector<DomainIndex::ListId> result;
    index.lookup("example.com", result);
    CHECK(result.empty());
}

TEST_CASE("DomainIndex: add_list idempotent") {
    DomainIndex::Builder builder;
    auto id1 = builder.add_list("my-list");
    auto id2 = builder.add_list("my-list");
    CHECK(id1 == id2);

    builder.add_domain(id1, "example.com");

    auto index = std::move(builder).build();
    CHECK(index.list_names().size() == 1);
    CHECK(index.list_names()[0] == "my-list");
}

TEST_CASE("DomainIndex: scale smoke test") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("list-0");
    auto list_b = builder.add_list("list-1");
    auto list_c = builder.add_list("list-2");

    const std::size_t num_domains = 100000;
    for (std::size_t i = 0; i < num_domains; ++i) {
        std::string domain = "d" + std::to_string(i) + ".example" +
                           std::to_string(i % 100) + ".net";

        // Distribute domains across lists
        builder.add_domain(list_a, domain);
        if (i % 2 == 0) {
            builder.add_domain(list_b, domain);
        }
        if (i % 3 == 0) {
            builder.add_domain(list_c, domain);
        }
    }

    auto index = std::move(builder).build();

    CHECK(index.domain_count() == 100000);
    std::vector<DomainIndex::ListId> result;
    std::size_t missing = 0;
    for (int i = 0; i < 100000; ++i) {
        index.lookup("d" + std::to_string(i) + ".example" + std::to_string(i % 100) + ".net", result);
        if (result.empty() || result.front() != list_a) {
            ++missing;
        }
    }
    CHECK(missing == 0);

    // Check memory usage
    std::size_t memory = index.memory_bytes();
    CHECK(memory < 1600000); // ~10 bytes per slot at load 0.7
}

TEST_CASE("DomainIndex: empty and dot lookups") {
    DomainIndex::Builder builder;
    auto list_a = builder.add_list("A");
    builder.add_domain(list_a, "example.com");

    auto index = std::move(builder).build();

    std::vector<DomainIndex::ListId> result;
    index.lookup("", result);
    CHECK(result.empty());

    result.clear();
    index.lookup(".", result);
    CHECK(result.empty());
}

}  // namespace keen_pbr3
