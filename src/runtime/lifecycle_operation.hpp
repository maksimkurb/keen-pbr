#pragma once

#include "../util/traced_mutex.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

enum class LifecycleOperationType : uint8_t { ApplyConfig, Start, Stop, Restart };
enum class LifecycleOperationStatus : uint8_t { Pending, Running, Succeeded, Failed, Skipped };
enum class LifecycleOperationResult : uint8_t { Running, Succeeded, Failed };

struct LifecycleOperationStage {
    std::string id;
    std::string title;
    LifecycleOperationStatus status{LifecycleOperationStatus::Pending};
    std::string detail;
};

struct LifecycleOperationSnapshot {
    std::string id;
    LifecycleOperationType type{LifecycleOperationType::ApplyConfig};
    LifecycleOperationResult result{LifecycleOperationResult::Running};
    std::int64_t started_at{0};
    std::optional<std::int64_t> finished_at;
    std::string error;
    std::vector<LifecycleOperationStage> stages;
};

// Owns only the immutable-at-the-boundary operation snapshot.  It deliberately
// has no knowledge of runtime commands, DNS or process probing.
class LifecycleOperationStore {
public:
    std::optional<LifecycleOperationSnapshot> snapshot() const;
    void publish(LifecycleOperationSnapshot snapshot);

private:
    mutable TracedSharedMutex mutex_;
    std::optional<LifecycleOperationSnapshot> snapshot_ GUARDED_BY(mutex_);
};

// Serializes lifecycle operations and performs legal stage state transitions.
class LifecycleOperationCoordinator {
public:
    explicit LifecycleOperationCoordinator(LifecycleOperationStore& store) : store_(store) {}

    // Returns the active operation id when a concurrent request must be rejected.
    std::optional<std::string> begin(LifecycleOperationType type,
                                     std::vector<LifecycleOperationStage> stages,
                                     LifecycleOperationSnapshot& created);
    void start_stage(const std::string& id, const std::string& stage_id);
    void succeed_stage(const std::string& id, const std::string& stage_id,
                       std::string detail = {});
    void fail_stage(const std::string& id, const std::string& stage_id, std::string detail);
    void skip_stage(const std::string& id, const std::string& stage_id, std::string detail = {});
    void finish(const std::string& id, std::string error = {});

private:
    void mutate(const std::string& id,
                const std::function<void(LifecycleOperationSnapshot&)>& mutation);
    static std::int64_t now_seconds();

    LifecycleOperationStore& store_;
    TracedMutex mutex_;
    std::optional<LifecycleOperationSnapshot> active_ GUARDED_BY(mutex_);
    std::uint64_t sequence_ GUARDED_BY(mutex_){0};
};

const char* lifecycle_operation_type_name(LifecycleOperationType value);
const char* lifecycle_operation_status_name(LifecycleOperationStatus value);
const char* lifecycle_operation_result_name(LifecycleOperationResult value);

} // namespace keen_pbr3
