#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace keen_pbr3 {

// The rule a list `file` must follow when it is set through the web API. A
// path that is already in the config file is not checked (the file can still
// be edited by hand to anything).
//
// A path is accepted when it is absolute, has no control characters (NUL and
// 0x01-0x1f, 0x7f; any other byte is fine), has no empty component and no
// component that is exactly `.` or `..`, ends with .lst, .txt or .list, and lies
// inside the directory that holds the config file (or below it, at any depth).
// The same must hold after symbolic links are resolved.
enum class ListFileProblem {
    NotAbsolute,
    TooLong,
    ControlCharacter,
    BadExtension,
    EmptyComponent,
    DotComponent,
    OutsideConfigDirectory,
    ResolvesOutsideConfigDirectory,
};

struct ListFileViolation {
    ListFileProblem problem;
    // Plain English, names the rule that matched and the configuration directory.
    std::string message;
};

// The directory that holds the config file, as an absolute normalized path.
std::string config_directory_of(const std::string& config_path);

// Only the text of the path is looked at.
std::optional<ListFileViolation> list_file_path_violation(std::string_view path,
                                                          std::string_view config_dir);

// Same rule, and the path must also stay inside the configuration directory
// after symbolic links in the existing part of both are resolved.
std::optional<ListFileViolation> list_file_policy_violation(const std::string& path,
                                                            const std::string& config_dir);

} // namespace keen_pbr3
