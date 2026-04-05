#include <doctest/doctest.h>

#include "../src/daemon/list_service.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

using namespace keen_pbr3;

namespace {

std::filesystem::path make_temp_dir() {
    char path_template[] = "/tmp/keen-pbr-list-service-XXXXXX";
    const char* created = mkdtemp(path_template);
    if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    return std::filesystem::path(created);
}

} // namespace

TEST_CASE("select_remote_list_targets: refresh-all selects only URL-backed lists") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";

    ListConfig local_file;
    local_file.file = "/tmp/local.lst";

    ListConfig inline_only;
    inline_only.domains = std::vector<std::string>{"example.com"};

    config.lists = std::map<std::string, ListConfig>{
        {"inline", inline_only},
        {"local", local_file},
        {"remote", remote},
    };

    const auto selection = select_remote_list_targets(config, std::nullopt);

    CHECK(selection.ok());
    CHECK(selection.list_names.size() == 1);
    CHECK(selection.list_names.front() == "remote");
}

TEST_CASE("select_remote_list_targets: single URL-backed list is accepted") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    const auto selection = select_remote_list_targets(config, std::string("remote"));

    CHECK(selection.ok());
    CHECK(selection.list_names == std::vector<std::string>{"remote"});
}

TEST_CASE("select_remote_list_targets: unknown list returns not found") {
    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    const auto selection = select_remote_list_targets(config, std::string("missing"));

    CHECK(selection.error == RemoteListTargetSelectionError::NotFound);
    CHECK(selection.list_names.empty());
}

TEST_CASE("select_remote_list_targets: non-URL-backed list returns not remote") {
    Config config;

    ListConfig local_file;
    local_file.file = "/tmp/local.lst";
    config.lists = std::map<std::string, ListConfig>{{"local", local_file}};

    const auto selection = select_remote_list_targets(config, std::string("local"));

    CHECK(selection.error == RemoteListTargetSelectionError::NotRemote);
    CHECK(selection.list_names.empty());
}

TEST_CASE("should_reload_runtime_after_list_refresh: only relevant changes reload active runtime") {
    RemoteListsRefreshResult refresh_result;
    refresh_result.changed_lists = {"remote"};
    refresh_result.relevant_changed_lists = {"remote"};

    CHECK(should_reload_runtime_after_list_refresh(true, refresh_result));
    CHECK_FALSE(should_reload_runtime_after_list_refresh(false, refresh_result));

    refresh_result.relevant_changed_lists.clear();
    CHECK_FALSE(should_reload_runtime_after_list_refresh(true, refresh_result));
}

TEST_CASE("build_list_refresh_state_map: URL-backed lists expose last_updated metadata only") {
    const auto temp_dir = make_temp_dir();
    CacheManager cache_manager(temp_dir);
    cache_manager.ensure_dir();

    CacheMetadata metadata;
    metadata.download_time = "2026-04-05T12:34:56Z";
    cache_manager.save_metadata("remote", metadata);

    Config config;

    ListConfig remote;
    remote.url = "https://example.com/remote.txt";

    ListConfig local_file;
    local_file.file = "/tmp/local.lst";

    config.lists = std::map<std::string, ListConfig>{
        {"remote", remote},
        {"local", local_file},
    };

    const auto refresh_state = build_list_refresh_state_map(config, cache_manager);

    auto remote_it = refresh_state.find("remote");
    CHECK(remote_it != refresh_state.end());
    REQUIRE(remote_it->second.last_updated.has_value());
    CHECK(*remote_it->second.last_updated == "2026-04-05T12:34:56Z");
    CHECK(refresh_state.find("local") == refresh_state.end());

    std::filesystem::remove_all(temp_dir);
}
