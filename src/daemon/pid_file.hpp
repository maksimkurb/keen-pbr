#pragma once

#include <cstdint>
#include <filesystem>

namespace keen_pbr3 {

class PidFile {
public:
    PidFile() = default;
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    void acquire(const std::filesystem::path& path);
    void remove();
    bool acquired() const noexcept { return fd_ >= 0; }

private:
    int fd_{-1};
    std::filesystem::path path_;
    std::uint64_t device_{0};
    std::uint64_t inode_{0};
};

} // namespace keen_pbr3
