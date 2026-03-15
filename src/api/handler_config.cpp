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
#include <stdexcept>
#include <string>
#include <shared_mutex>
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

} // namespace

void register_config_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/config - return current config and whether it is staged in memory
    server.get("/api/config", [&ctx]() -> std::string {
        std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
        nlohmann::json response = {
            {"config", nlohmann::json(ctx.visible_config_fn())},
            {"is_draft", ctx.config_is_draft_fn()},
        };
        return response.dump();
    });

    // POST /api/config - validate and stage in memory only
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        Config staged;
        try {
            staged = parse_config(body);
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

        ctx.stage_config_fn(std::move(staged), body);

        api::ConfigUpdateResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Config staged in memory";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/save - dry-run check, persist staged config, apply immediately
    server.post("/api/config/save", [&ctx]() -> std::string {
        {
            std::lock_guard<std::mutex> op_lock(ctx.config_op_mutex);
            if (ctx.config_op_state.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            ctx.config_op_state.store(ConfigOperationState::Saving, std::memory_order_release);
        }

        auto finish_operation = [&ctx]() {
            std::lock_guard<std::mutex> op_lock(ctx.config_op_mutex);
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            ctx.config_op_cv.notify_all();
        };

        std::optional<std::pair<Config, std::string>> staged_snapshot;
        try {
            std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
            staged_snapshot = ctx.staged_config_snapshot_fn();
        } catch (...) {
            finish_operation();
            throw;
        }

        if (!staged_snapshot.has_value()) {
            finish_operation();
            throw ApiError("No staged config to save", 400);
        }

        // Phase 1: validation + dry-run apply check.
        try {
            ctx.dry_run_apply_check_fn(staged_snapshot->first);
        } catch (const std::exception& e) {
            finish_operation();
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
                ctx.enqueue_apply_validated_config_fn(staged_snapshot->first, staged_snapshot->second);

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
            finish_operation();
            return response.dump();
        } catch (...) {
            finish_operation();
            throw;
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
