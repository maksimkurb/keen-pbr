#include <doctest/doctest.h>

#include "../src/lists/list_streamer.hpp"

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
        char pattern[] = "/tmp/keen-pbr-list-streamer-XXXXXX";
        const char* value = ::mkdtemp(pattern);
        if (!value) throw std::runtime_error("mkdtemp failed");
        path_ = value;
    }
    ~TempDirectory() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

class CountingVisitor : public ListEntryVisitor {
public:
    void on_entry(EntryType, std::string_view) override { ++count; }
    std::size_t count{0};
};

void stream_local(const std::filesystem::path& path, std::size_t max_size) {
    CacheManager cache(path.parent_path() / "cache", max_size);
    ListStreamer streamer(cache);
    ListConfig config;
    config.file = path.string();
    CountingVisitor visitor;
    streamer.stream_list("local", config, visitor);
}

} // namespace

TEST_CASE("ListStreamer reads bounded regular local files") {
    TempDirectory temp;
    const auto path = temp.path() / "list.txt";
    { std::ofstream out(path); out << "example.com\n192.0.2.0/24\n"; }

    CacheManager cache(temp.path() / "cache", 1024);
    ListStreamer streamer(cache);
    ListConfig config;
    config.file = path.string();
    CountingVisitor visitor;
    streamer.stream_list("local", config, visitor);
    CHECK(visitor.count == 2);
}

TEST_CASE("ListStreamer rejects symlinks and non-regular files") {
    TempDirectory temp;
    const auto regular = temp.path() / "regular.txt";
    const auto symlink = temp.path() / "symlink.txt";
    const auto fifo = temp.path() / "fifo";
    { std::ofstream out(regular); out << "example.com\n"; }
    std::filesystem::create_symlink(regular, symlink);
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);

    CHECK_THROWS(stream_local(symlink, 1024));
    CHECK_THROWS(stream_local(fifo, 1024));
    CHECK_THROWS(stream_local("/dev/zero", 1024));
}

TEST_CASE("ListStreamer rejects files and lines over configured bounds") {
    TempDirectory temp;
    const auto oversized = temp.path() / "oversized.txt";
    const auto long_line = temp.path() / "long-line.txt";
    { std::ofstream out(oversized); out << std::string(65, 'a'); }
    { std::ofstream out(long_line); out << std::string(ListStreamer::kMaxLineBytes + 1, 'a'); }

    CHECK_THROWS(stream_local(oversized, 64));
    CHECK_THROWS(stream_local(long_line, 8192));
}

} // namespace keen_pbr3
