#include "list_file_policy.hpp"

#include <array>
#include <filesystem>
#include <system_error>

namespace keen_pbr3 {
namespace {

constexpr std::size_t kMaxPathBytes = 4096;
constexpr std::array<std::string_view, 3> kExtensions{".lst", ".txt", ".list"};

bool control_character(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return byte < 0x20 || byte == 0x7f;
}

bool under(std::string_view path, std::string_view directory) {
    while (directory.size() > 1 && directory.back() == '/') directory.remove_suffix(1);
    if (directory == "/") return true;
    return path.size() > directory.size() && path.substr(0, directory.size()) == directory &&
           path[directory.size()] == '/';
}

std::string quote_path(std::string_view path) {
    return "\"" + std::string(path) + "\"";
}

ListFileViolation violation(ListFileProblem problem, std::string message) {
    return ListFileViolation{problem, std::move(message)};
}

bool has_allowed_extension(std::string_view path) {
    for (const auto extension : kExtensions) {
        if (path.size() > extension.size() &&
            path.substr(path.size() - extension.size()) == extension) {
            return true;
        }
    }
    return false;
}

std::optional<ListFileViolation> check_path(std::string_view path, std::string_view config_dir,
                                            bool check_directory) {
    if (path.empty() || path.front() != '/') {
        return violation(ListFileProblem::NotAbsolute,
                         quote_path(path) + " is not an absolute path; it must start with /");
    }
    if (path.size() > kMaxPathBytes) {
        return violation(ListFileProblem::TooLong, "the path is longer than 4096 bytes");
    }
    for (const char ch : path) {
        if (control_character(ch)) {
            // The path is not repeated: it could forge text in a log or on a screen.
            return violation(ListFileProblem::ControlCharacter,
                             "the path contains a NUL or other control character, which is not allowed");
        }
    }
    std::size_t start = 1;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto component = path.substr(start, end == std::string_view::npos
                                                      ? std::string_view::npos
                                                      : end - start);
        if (component.empty()) {
            return violation(ListFileProblem::EmptyComponent,
                             quote_path(path) + " has an empty part (a doubled or trailing /)");
        }
        if (component == "." || component == "..") {
            return violation(ListFileProblem::DotComponent,
                             quote_path(path) + " has a \"" + std::string(component) +
                                 "\" part, which is not allowed");
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (!has_allowed_extension(path)) {
        return violation(ListFileProblem::BadExtension,
                         quote_path(path) + " must end with .lst, .txt or .list");
    }
    if (check_directory && !under(path, config_dir)) {
        return violation(ListFileProblem::OutsideConfigDirectory,
                         quote_path(path) + " is outside the configuration directory " +
                             std::string(config_dir) + "; lists may only use files inside it");
    }
    return std::nullopt;
}

std::string canonical_or_self(const std::string& path) {
    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    return ec ? path : resolved.string();
}

} // namespace

std::string config_directory_of(const std::string& config_path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(std::filesystem::path(config_path), ec);
    if (ec) absolute = std::filesystem::path(config_path);
    std::string directory = absolute.lexically_normal().parent_path().string();
    while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
    return directory;
}

std::optional<ListFileViolation> list_file_path_violation(std::string_view path,
                                                          std::string_view config_dir) {
    return check_path(path, config_dir, true);
}

std::optional<ListFileViolation> list_file_policy_violation(const std::string& path,
                                                            const std::string& config_dir) {
    if (auto found = check_path(path, config_dir, true)) return found;
    // Both sides are resolved, so a config directory that is itself reached
    // through a link (/opt on some routers) works naturally.
    const std::string resolved = canonical_or_self(path);
    const std::string resolved_dir = canonical_or_self(config_dir);
    if (!under(resolved, resolved_dir)) {
        return violation(ListFileProblem::ResolvesOutsideConfigDirectory,
                         quote_path(path) + " resolves through a symbolic link to " +
                             quote_path(resolved) + ", which is outside the configuration directory " +
                             config_dir + "; lists may only use files inside it");
    }
    if (!has_allowed_extension(resolved)) {
        return violation(ListFileProblem::ResolvesOutsideConfigDirectory,
                         quote_path(path) + " resolves through a symbolic link to " +
                             quote_path(resolved) + ", which must end with .lst, .txt or .list");
    }
    return std::nullopt;
}

} // namespace keen_pbr3
