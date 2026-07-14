#include <doctest/doctest.h>

#include "../src/config/config_writer.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace keen_pbr3 {
namespace {

class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/keen-pbr-config-writer-XXXXXX";
        const char* created = ::mkdtemp(pattern);
        REQUIRE(created != nullptr);
        path = created;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

mode_t file_mode(const std::filesystem::path& path) {
    struct stat st {};
    REQUIRE(::stat(path.c_str(), &st) == 0);
    return st.st_mode & 07777;
}

} // namespace

TEST_CASE("atomic config writer preserves existing mode") {
    TempDir dir;
    const auto config = dir.path / "config.json";
    { std::ofstream output(config); output << "old"; }
    REQUIRE(::chmod(config.c_str(), 0640) == 0);

    write_config_atomically(config.string(), "new");

    CHECK(read_file(config) == "new");
    CHECK(file_mode(config) == 0640);
}

TEST_CASE("atomic config writer creates private files") {
    TempDir dir;
    const auto config = dir.path / "config.json";
    write_config_atomically(config.string(), "new");
    CHECK(file_mode(config) == 0600);
}

TEST_CASE("atomic config writer refuses a symlink destination") {
    TempDir dir;
    const auto sentinel = dir.path / "sentinel";
    const auto config = dir.path / "config.json";
    { std::ofstream output(sentinel); output << "unchanged"; }
    REQUIRE(::symlink(sentinel.c_str(), config.c_str()) == 0);

    CHECK_THROWS(write_config_atomically(config.string(), "clobbered"));
    CHECK(read_file(sentinel) == "unchanged");
}

} // namespace keen_pbr3
