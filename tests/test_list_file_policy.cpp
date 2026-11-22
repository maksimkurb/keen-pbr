#include <doctest/doctest.h>

#include "../src/config/list_file_policy.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace keen_pbr3 {
namespace {

constexpr std::string_view kDir = "/etc/keen-pbr";

ListFileViolation refused(std::string_view path, std::string_view dir = kDir) {
    const auto found = list_file_path_violation(path, dir);
    REQUIRE(found.has_value());
    return *found;
}

} // namespace

TEST_CASE("List file policy takes the directory of the config file") {
    CHECK(config_directory_of("/etc/keen-pbr/config.json") == "/etc/keen-pbr");
    CHECK(config_directory_of("/opt/etc/keen-pbr/config.json") == "/opt/etc/keen-pbr");
    CHECK(config_directory_of("/config.json") == "/");
    CHECK(config_directory_of("config.json") == std::filesystem::current_path().string());
}

TEST_CASE("List file policy accepts files inside the configuration directory") {
    for (const char* path : {
             "/etc/keen-pbr/list.lst",
             "/etc/keen-pbr/list.txt",
             "/etc/keen-pbr/list.list",
             "/etc/keen-pbr/lists/sub/folder/list.lst",
             "/etc/keen-pbr/sub dir/a b.lst",
             "/etc/keen-pbr/\xd0\xa1\xd0\xbf\xd0\xb8\xd1\x81\xd0\xbe\xd0\xba/\xd0\xb4\xd0\xbe\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x8b.lst",
             "/etc/keen-pbr/list (1).lst",
             "/etc/keen-pbr/100%/a&b;c'd.txt",
             "/etc/keen-pbr/a%2e%2e/b&lt;.list",
             "/etc/keen-pbr/ lead/trail /x.lst",
             "/etc/keen-pbr/a..b/.../x.lst",
             "/etc/keen-pbr/.lst.lst",
         }) {
        INFO(path);
        CHECK_FALSE(list_file_path_violation(path, kDir).has_value());
    }
    CHECK_FALSE(list_file_path_violation("/opt/etc/keen-pbr/lists/a.lst",
                                         "/opt/etc/keen-pbr").has_value());
    CHECK_FALSE(list_file_path_violation("/any/where.lst", "/").has_value());
}

TEST_CASE("List file policy refuses everything else") {
    for (const char* path : {
             "",
             "list.lst",
             "./list.lst",
             "etc/keen-pbr/list.lst",
             "/etc/list.lst",
             "/etc/passwd.lst",
             "/etc/keen-pbr-evil/a.lst",
             "/etc/keen-pbr.lst",
             "/etc/keen-pbr",
             "/etc/keen-pbr/",
             "/opt/etc/keen-pbr/list.lst",
             "/etc/keen-pbr/../list.lst",
             "/etc/keen-pbr/lists/../../passwd.lst",
             "/etc/keen-pbr/./list.lst",
             "/etc/keen-pbr//list.lst",
             "/etc/keen-pbr/list.json",
             "/etc/keen-pbr/list.LST",
             "/etc/keen-pbr/list.lst.bak",
             "/etc/keen-pbr/list",
             "/etc/keen-pbr/a\tb.lst",
             "/etc/keen-pbr/a\nb.lst",
             "/etc/keen-pbr/a\x7f.lst",
             "/etc/keen-pbr/a\x1b[0m.lst",
             "/etc/keen-pbr/..",
         }) {
        INFO(path);
        CHECK(list_file_path_violation(path, kDir).has_value());
    }
    CHECK(list_file_path_violation(std::string("/etc/keen-pbr/a\0b.lst", 21), kDir).has_value());
    CHECK(list_file_path_violation("/etc/keen-pbr/" + std::string(5000, 'a') + ".lst", kDir)
              .has_value());
}

TEST_CASE("List file policy says which rule matched") {
    {
        const auto v = refused("list.lst");
        CHECK(v.problem == ListFileProblem::NotAbsolute);
        CHECK(v.message == "\"list.lst\" is not an absolute path; it must start with /");
    }
    {
        const auto v = refused("/etc/keen-pbr/x.json");
        CHECK(v.problem == ListFileProblem::BadExtension);
        CHECK(v.message == "\"/etc/keen-pbr/x.json\" must end with .lst, .txt or .list");
    }
    {
        const auto v = refused(std::string("/etc/keen-pbr/a\0b.lst", 21));
        CHECK(v.problem == ListFileProblem::ControlCharacter);
        CHECK(v.message.find("control character") != std::string::npos);
        // The offending bytes are not repeated.
        CHECK(v.message.find("/etc") == std::string::npos);
    }
    {
        const auto v = refused("/etc/keen-pbr/../x.lst");
        CHECK(v.problem == ListFileProblem::DotComponent);
        CHECK(v.message == "\"/etc/keen-pbr/../x.lst\" has a \"..\" part, which is not allowed");
    }
    {
        const auto v = refused("/etc/keen-pbr//x.lst");
        CHECK(v.problem == ListFileProblem::EmptyComponent);
        CHECK(v.message == "\"/etc/keen-pbr//x.lst\" has an empty part (a doubled or trailing /)");
    }
    {
        const auto v = refused("/etc/list.lst");
        CHECK(v.problem == ListFileProblem::OutsideConfigDirectory);
        CHECK(v.message == "\"/etc/list.lst\" is outside the configuration directory "
                           "/etc/keen-pbr; lists may only use files inside it");
    }
    CHECK(refused("/etc/keen-pbr-evil/a.lst").problem == ListFileProblem::OutsideConfigDirectory);
}

TEST_CASE("List file policy also checks where the path really leads") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
                          ("keen-pbr-policy-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "conf" / "lists");
    fs::create_directories(root / "outside");
    std::ofstream(root / "outside" / "secret.lst") << "x\n";
    std::ofstream(root / "conf" / "lists" / "ok.lst") << "x\n";
    fs::create_symlink(root / "outside", root / "conf" / "dirlink");
    fs::create_symlink(root / "outside" / "secret.lst", root / "conf" / "filelink.lst");
    fs::create_symlink(root / "conf", root / "conf-alias");

    const std::string dir = (root / "conf").string();
    const auto check = [&](const fs::path& path) {
        return list_file_policy_violation(path.string(), dir);
    };
    CHECK_FALSE(check(root / "conf" / "lists" / "ok.lst").has_value());
    CHECK_FALSE(check(root / "conf" / "lists" / "not-there-yet.txt").has_value());
    {
        const auto v = check(root / "conf" / "dirlink" / "secret.lst");
        REQUIRE(v.has_value());
        CHECK(v->problem == ListFileProblem::ResolvesOutsideConfigDirectory);
        CHECK(v->message.find("outside the configuration directory " + dir) != std::string::npos);
    }
    {
        const auto v = check(root / "conf" / "filelink.lst");
        REQUIRE(v.has_value());
        CHECK(v->problem == ListFileProblem::ResolvesOutsideConfigDirectory);
    }
    // A configuration directory reached through a link works: both sides are resolved.
    CHECK_FALSE(list_file_policy_violation((root / "conf-alias" / "lists" / "ok.lst").string(),
                                           (root / "conf-alias").string()).has_value());
    fs::remove_all(root);
}

} // namespace keen_pbr3
