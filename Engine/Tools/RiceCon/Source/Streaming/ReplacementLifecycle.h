#pragma once
#include "SlotModel.h"
#include <functional>
#include <future>
#include <map>
namespace RiceController { class ProcessManager; }

namespace RiceController::Streaming {
// Controller-thread coordinator. Commit is called only after the media worker
// confirms the replacement source at a frame boundary, never during preparation.
class ReplacementLifecycle {
public:
    struct Instance { std::string entryId, runtimeId; };
    // Save must quiesce this retired instance and acknowledge durable writes,
    // including its Player-side writes. Saving Controller settings is not enough.
    using Save = std::function<std::future<void>(const Instance&)>;
    using Stop = std::function<void(const Instance&)>;
    using IsCurrent = std::function<bool(const Instance&)>;
    ReplacementLifecycle(Save save, Stop stop, IsCurrent current);
    ReplacementLifecycle(Save save, ProcessManager& processes);
    explicit ReplacementLifecycle(ProcessManager& processes);
    void Commit(SlotRegistry& slots, const std::string& slotId, const Instance& next,
                const std::string& instanceTypeId);
    void Poll(const SlotRegistry& slots);
    void Retry(const std::string& runtimeId);
    void RetireUnused(const SlotRegistry& slots,const Instance& instance);
    std::string Error(const std::string& runtimeId) const;
    std::size_t Pending() const noexcept { return pending_.size(); }
private:
    struct Retirement { Instance instance; std::future<void> saved; std::string error; };
    Save save_; Stop stop_; IsCurrent current_;
    std::map<std::string, Retirement> pending_;
    void BeginSave(Retirement& item);
    static bool InUse(const SlotRegistry& slots, const std::string& runtimeId);
};
}
