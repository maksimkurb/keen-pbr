#include "config_writer.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace keen_pbr3 {
namespace {

std::runtime_error errno_error(const std::string& prefix) {
    return std::runtime_error(prefix + ": " + std::strerror(errno));
}

void write_all(int fd, const std::string& body) {
    size_t offset = 0;
    while (offset < body.size()) {
        const ssize_t written = ::write(fd, body.data() + offset, body.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw errno_error("Cannot write config file");
        }
        offset += static_cast<size_t>(written);
    }
}

void fsync_fd(int fd, const std::string& what) {
    if (::fsync(fd) != 0) throw errno_error("Cannot fsync " + what);
}

} // namespace

void write_config_atomically(const std::string& config_path,
                             const std::string& body) {
    const std::filesystem::path path(config_path);
    const auto directory = path.has_parent_path() ? path.parent_path()
                                                   : std::filesystem::path(".");

    struct stat existing {};
    const bool exists = ::lstat(config_path.c_str(), &existing) == 0;
    if (!exists && errno != ENOENT) throw errno_error("Cannot inspect config file");
    if (exists && !S_ISREG(existing.st_mode)) {
        throw std::runtime_error("Refusing to replace non-regular config file");
    }

    const mode_t mode = exists ? (existing.st_mode & 07777) : (S_IRUSR | S_IWUSR);
    std::string tmp_template =
        (directory / ("." + path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> tmp_name(tmp_template.begin(), tmp_template.end());
    tmp_name.push_back('\0');

    int tmp_fd = ::mkstemp(tmp_name.data());
    if (tmp_fd < 0) throw errno_error("Cannot create temporary config file");
    const std::string tmp_path(tmp_name.data());

    try {
        if (::fcntl(tmp_fd, F_SETFD, FD_CLOEXEC) != 0) {
            throw errno_error("Cannot mark temporary config file close-on-exec");
        }
        if (exists && ::fchown(tmp_fd, existing.st_uid, existing.st_gid) != 0) {
            throw errno_error("Cannot preserve config file ownership");
        }
        if (::fchmod(tmp_fd, mode) != 0) {
            throw errno_error("Cannot set config file mode");
        }
        write_all(tmp_fd, body);
        fsync_fd(tmp_fd, "temporary config file");
        if (::close(tmp_fd) != 0) {
            tmp_fd = -1;
            throw errno_error("Cannot close temporary config file");
        }
        tmp_fd = -1;

        if (::rename(tmp_path.c_str(), config_path.c_str()) != 0) {
            throw errno_error("Cannot replace config file");
        }

        const int dir_fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd < 0) throw errno_error("Cannot open config directory for fsync");
        try {
            fsync_fd(dir_fd, "config directory");
        } catch (...) {
            ::close(dir_fd);
            throw;
        }
        if (::close(dir_fd) != 0) {
            throw errno_error("Cannot close config directory after fsync");
        }
    } catch (...) {
        if (tmp_fd >= 0) ::close(tmp_fd);
        ::unlink(tmp_path.c_str());
        throw;
    }
}

} // namespace keen_pbr3
