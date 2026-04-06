#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../config/config.hpp"
#include "../log/logger.hpp"
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

std::runtime_error make_errno_error(const std::string& prefix) {
    return std::runtime_error(prefix + ": " + std::strerror(errno));
}

void cleanup_tmp_file(const std::string& tmp_path) {
    if (std::remove(tmp_path.c_str()) != 0 && errno != ENOENT) {
        Logger::instance().warn("Failed to remove temporary config file '{}': {}", tmp_path, std::strerror(errno));
    }
}

void write_all_or_throw(int fd, const std::string& body) {
    const char* data = body.data();
    size_t total_written = 0;
    while (total_written < body.size()) {
        const ssize_t written = ::write(fd, data + total_written, body.size() - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw make_errno_error("Cannot write config file");
        }
        total_written += static_cast<size_t>(written);
    }
}

void fsync_or_throw(int fd, const std::string& what) {
    if (::fsync(fd) != 0) {
        throw make_errno_error("Cannot fsync " + what);
    }
}

void write_config_atomically(const std::string& config_path,
                             const std::string& body) {
    const std::filesystem::path config_fs_path(config_path);
    const std::filesystem::path dir_path = config_fs_path.has_parent_path()
                                               ? config_fs_path.parent_path()
                                               : std::filesystem::path(".");
    const std::string tmp_path = config_path + ".tmp";

    int tmp_fd = -1;
    try {
        tmp_fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (tmp_fd < 0) {
            throw make_errno_error("Cannot open temporary config file");
        }

        write_all_or_throw(tmp_fd, body);
        fsync_or_throw(tmp_fd, "temporary config file");

        if (::close(tmp_fd) != 0) {
            tmp_fd = -1;
            throw make_errno_error("Cannot close temporary config file");
        }
        tmp_fd = -1;

        if (std::rename(tmp_path.c_str(), config_path.c_str()) != 0) {
            throw make_errno_error("Cannot replace config file");
        }

        const auto dir_path_string = dir_path.string();
        const int dir_fd = ::open(dir_path_string.c_str(), O_RDONLY | O_CLOEXEC);
        if (dir_fd < 0) {
            throw make_errno_error("Cannot open config directory for fsync");
        }

        try {
            fsync_or_throw(dir_fd, "config directory");
        } catch (...) {
            ::close(dir_fd);
            throw;
        }

        if (::close(dir_fd) != 0) {
            throw make_errno_error("Cannot close config directory after fsync");
        }
    } catch (...) {
        if (tmp_fd >= 0) {
            ::close(tmp_fd);
        }
        cleanup_tmp_file(tmp_path);
        throw;
    }
}

nlohmann::json make_validation_error_json(const ConfigValidationError& error) {
    nlohmann::json issues = nlohmann::json::array();
    for (const auto& issue : error.issues()) {
        issues.push_back({
            {"path", issue.path},
            {"message", issue.message},
        });
    }

    return {
        {"error", error.what()},
        {"validation_errors", std::move(issues)},
    };
}

std::string serialize_config_pretty(const Config& config) {
    nlohmann::json json = config;
    std::function<bool(nlohmann::json&)> prune_json = [&](nlohmann::json& value) -> bool {
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end();) {
                if (prune_json(it.value())) {
                    it = value.erase(it);
                } else {
                    ++it;
                }
            }
            return value.empty();
        }

        if (value.is_array()) {
            for (auto& item : value) {
                (void)prune_json(item);
            }
            return false;
        }

        return value.is_null();
    };

    (void)prune_json(json);
    return json.dump(1, '\t') + "\n";
}

} // namespace

void register_config_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/config - return current config and whether it is staged in memory
    server.get("/api/config", [&ctx]() -> std::string {
        const Config visible_config = ctx.get_visible_config();
        const bool is_draft = ctx.config_is_draft();
        const auto list_refresh_state = ctx.get_list_refresh_state_map(visible_config);
        nlohmann::json response = {
            {"config", nlohmann::json(visible_config)},
            {"is_draft", is_draft},
            {"list_refresh_state", nlohmann::json(list_refresh_state)},
        };
        return response.dump();
    });

    // POST /api/config - validate and stage in memory only
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        Config staged;
        try {
            staged = parse_config(body);
            validate_config(staged);
        } catch (const ConfigValidationError& e) {
            throw ApiError(e.what(), 400, make_validation_error_json(e).dump());
        } catch (const ConfigError& e) {
            nlohmann::json payload = {
                {"error", e.what()},
                {"validation_errors", nlohmann::json::array({
                    {{"path", "$"}, {"message", e.what()}},
                })},
            };
            throw ApiError(e.what(), 400, payload.dump());
        }

        std::string formatted_config = serialize_config_pretty(staged);
        ctx.stage_config(std::move(staged), std::move(formatted_config));

        api::ConfigUpdateResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Config staged in memory";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/save - dry-run check, persist staged config, apply immediately
    server.post("/api/config/save", [&ctx]() -> std::string {
        ctx.begin_save_operation();

        std::optional<std::pair<Config, std::string>> staged_snapshot;
        try {
            staged_snapshot = ctx.get_staged_config_snapshot();
        } catch (...) {
            ctx.finish_config_operation();
            throw;
        }

        if (!staged_snapshot.has_value()) {
            ctx.finish_config_operation();
            throw ApiError("No staged config to save", 400);
        }

        // Phase 1: validation + dry-run apply check.
        try {
            ctx.validate_candidate_config(staged_snapshot->first);
        } catch (const ConfigValidationError& e) {
            ctx.finish_config_operation();
            nlohmann::json error_payload = make_validation_error_json(e);
            error_payload["saved"] = false;
            error_payload["applied"] = false;
            error_payload["rolled_back"] = false;
            throw ApiError("Dry-run apply check failed", 400, error_payload.dump());
        } catch (const std::exception& e) {
            ctx.finish_config_operation();
            nlohmann::json error_payload = {
                {"error", std::string("Dry-run apply check failed: ") + e.what()},
                {"saved", false},
                {"applied", false},
                {"rolled_back", false},
            };
            throw ApiError("Dry-run apply check failed", 500, error_payload.dump());
        }

        // Phase 2: write + commit/apply.
        try {
            write_config_atomically(ctx.config_path, staged_snapshot->second);
            ConfigApplyResult apply_result =
                ctx.enqueue_apply_validated_config(staged_snapshot->first, staged_snapshot->second);

            if (!apply_result.error.empty()) {
                nlohmann::json error_payload = {
                    {"error", std::string("Commit/apply failed: ") + apply_result.error},
                    {"saved", true},
                    {"applied", apply_result.applied},
                    {"rolled_back", apply_result.rolled_back},
                };
                throw ApiError("Commit/apply failed", 500, error_payload.dump());
            }

            nlohmann::json response = {
                {"status", "ok"},
                {"message", "Config saved and applied"},
                {"saved", true},
                {"applied", apply_result.applied},
                {"rolled_back", apply_result.rolled_back},
            };
            if (apply_result.apply_started_ts.has_value()) {
                response["apply_started_ts"] = *apply_result.apply_started_ts;
            }
            ctx.finish_config_operation();
            return response.dump();
        } catch (...) {
            ctx.finish_config_operation();
            throw;
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
