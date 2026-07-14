#include <doctest/doctest.h>

#include "../src/daemon/pid_file.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace keen_pbr3 {
namespace {

class TempDirectory {
public:
    TempDirectory() {
        char pattern[] = "/tmp/keen-pbr-pid-XXXXXX";
        const char* path = ::mkdtemp(pattern);
        if (!path) throw std::runtime_error("mkdtemp failed");
        path_ = path;
    }
    ~TempDirectory() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("PidFile acquires, writes, and removes its file") {
    TempDirectory temp;
    const auto path = temp.path() / "daemon.pid";
    PidFile pid;
    pid.acquire(path);
    CHECK(pid.acquired());
    CHECK(std::filesystem::exists(path));
    pid.remove();
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("PidFile refuses symlinks and unrelated regular files") {
    TempDirectory temp;
    const auto sentinel = temp.path() / "sentinel";
    const auto link = temp.path() / "daemon.pid";
    { std::ofstream out(sentinel); out << "do not touch\n"; }
    std::filesystem::create_symlink(sentinel, link);

    PidFile pid;
    CHECK_THROWS(pid.acquire(link));
    std::ifstream input(sentinel);
    std::string value;
    std::getline(input, value);
    CHECK(value == "do not touch");

    std::filesystem::remove(link);
    CHECK_THROWS(pid.acquire(sentinel));
}

TEST_CASE("PidFile removal preserves a replacement inode") {
    TempDirectory temp;
    const auto path = temp.path() / "daemon.pid";
    const auto old_path = temp.path() / "old.pid";
    PidFile pid;
    pid.acquire(path);
    std::filesystem::rename(path, old_path);
    { std::ofstream out(path); out << "sentinel\n"; }

    pid.remove();
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("PidFile accepts a locked stale PID file") {
    TempDirectory temp;
    const auto path = temp.path() / "daemon.pid";
    { std::ofstream out(path); out << "12345\n"; }
    CHECK(::chmod(path.c_str(), 0644) == 0);

    PidFile pid;
    CHECK_NOTHROW(pid.acquire(path));
}

} // namespace keen_pbr3
