#include "lifecycle_operation.hpp"

#include <algorithm>
#include <ctime>
#include <functional>

namespace keen_pbr3 {

const char* lifecycle_operation_type_name(LifecycleOperationType value) {
    switch (value) {
    case LifecycleOperationType::ApplyConfig: return "apply_config";
    case LifecycleOperationType::Start: return "start";
    case LifecycleOperationType::Stop: return "stop";
    case LifecycleOperationType::Restart: return "restart";
    }
    return "apply_config";
}

const char* lifecycle_operation_status_name(LifecycleOperationStatus value) {
    switch (value) {
    case LifecycleOperationStatus::Pending: return "pending";
    case LifecycleOperationStatus::Running: return "running";
    case LifecycleOperationStatus::Succeeded: return "succeeded";
    case LifecycleOperationStatus::Failed: return "failed";
    case LifecycleOperationStatus::Skipped: return "skipped";
    }
    return "pending";
}

const char* lifecycle_operation_result_name(LifecycleOperationResult value) {
    switch (value) {
    case LifecycleOperationResult::Running: return "running";
    case LifecycleOperationResult::Succeeded: return "succeeded";
    case LifecycleOperationResult::Failed: return "failed";
    }
    return "running";
}

std::optional<LifecycleOperationSnapshot> LifecycleOperationStore::snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return snapshot_;
}

void LifecycleOperationStore::publish(LifecycleOperationSnapshot snapshot) {
    std::function<void()> callback;
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        snapshot_ = std::move(snapshot);
        callback = publish_callback_;
    }
    if (callback) callback();
}

void LifecycleOperationStore::set_publish_callback(std::function<void()> callback) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    publish_callback_ = std::move(callback);
}

std::int64_t LifecycleOperationCoordinator::now_seconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::optional<std::string> LifecycleOperationCoordinator::begin(
    LifecycleOperationType type, std::vector<LifecycleOperationStage> stages,
    LifecycleOperationSnapshot& created) {
    KPBR_LOCK_GUARD(mutex_);
    if (active_) return active_->id;
    created.id = "lifecycle-" + std::to_string(++sequence_);
    created.type = type;
    created.result = LifecycleOperationResult::Running;
    created.started_at = now_seconds();
    created.finished_at.reset();
    created.error.clear();
    created.stages = std::move(stages);
    active_ = created;
    store_.publish(created);
    return std::nullopt;
}

void LifecycleOperationCoordinator::mutate(
    const std::string& id, const std::function<void(LifecycleOperationSnapshot&)>& mutation) {
    KPBR_LOCK_GUARD(mutex_);
    if (!active_ || active_->id != id) return;
    mutation(*active_);
    store_.publish(*active_);
}

void LifecycleOperationCoordinator::start_stage(const std::string& id, const std::string& stage_id) {
    mutate(id, [&](auto& op) {
        auto it = std::find_if(op.stages.begin(), op.stages.end(), [&](const auto& stage) {
            return stage.id == stage_id;
        });
        if (it != op.stages.end() && it->status == LifecycleOperationStatus::Pending)
            it->status = LifecycleOperationStatus::Running;
    });
}

void LifecycleOperationCoordinator::succeed_stage(const std::string& id, const std::string& stage_id,
                                                   std::string detail) {
    mutate(id, [&](auto& op) {
        for (auto& stage : op.stages) if (stage.id == stage_id) {
            if (stage.status != LifecycleOperationStatus::Running) break;
            stage.status = LifecycleOperationStatus::Succeeded;
            stage.detail = std::move(detail);
            break;
        }
    });
}

void LifecycleOperationCoordinator::fail_stage(const std::string& id, const std::string& stage_id,
                                                std::string detail) {
    mutate(id, [&](auto& op) {
        bool failed = false;
        for (auto& stage : op.stages) if (stage.id == stage_id) {
            if (stage.status != LifecycleOperationStatus::Pending &&
                stage.status != LifecycleOperationStatus::Running) break;
            stage.status = LifecycleOperationStatus::Failed;
            stage.detail = detail;
            op.error = std::move(detail);
            failed = true;
            break;
        }
        if (!failed) return;
        for (auto& stage : op.stages) {
            if (stage.status == LifecycleOperationStatus::Pending) {
                stage.status = LifecycleOperationStatus::Skipped;
            }
        }
    });
}

void LifecycleOperationCoordinator::skip_stage(const std::string& id, const std::string& stage_id,
                                                std::string detail) {
    mutate(id, [&](auto& op) {
        for (auto& stage : op.stages) if (stage.id == stage_id) {
            if (stage.status != LifecycleOperationStatus::Pending) break;
            stage.status = LifecycleOperationStatus::Skipped;
            stage.detail = std::move(detail);
            break;
        }
    });
}

void LifecycleOperationCoordinator::finish(const std::string& id, std::string error) {
    mutate(id, [&](auto& op) {
        if (!error.empty()) op.error = std::move(error);
        if (!op.error.empty()) {
            for (auto& stage : op.stages) {
                if (stage.status == LifecycleOperationStatus::Pending) {
                    stage.status = LifecycleOperationStatus::Skipped;
                }
            }
        }
        op.result = op.error.empty() ? LifecycleOperationResult::Succeeded
                                     : LifecycleOperationResult::Failed;
        op.finished_at = now_seconds();
    });
    KPBR_LOCK_GUARD(mutex_);
    if (active_ && active_->id == id) active_.reset();
}

} // namespace keen_pbr3
