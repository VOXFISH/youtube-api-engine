#include "ReplacementLifecycle.h"
#include "../ProcessManager.h"
#include <chrono>
#include <stdexcept>

namespace RiceController::Streaming {
ReplacementLifecycle::ReplacementLifecycle(ProcessManager& processes)
    : ReplacementLifecycle([&processes](const Instance& instance) {
        return processes.SaveForRetirement(instance.entryId,instance.runtimeId);
    },processes) {}
ReplacementLifecycle::ReplacementLifecycle(Save save, ProcessManager& processes)
    : ReplacementLifecycle(std::move(save),
        [&processes](const Instance& instance) { processes.StopIfCurrent(instance.entryId,instance.runtimeId); },
        [&processes](const Instance& instance) {
            const auto status=processes.Get(instance.entryId);
            return status.alive && status.runId==instance.runtimeId;
        }) {}
ReplacementLifecycle::ReplacementLifecycle(Save save, Stop stop, IsCurrent current)
    : save_(std::move(save)), stop_(std::move(stop)), current_(std::move(current)) {
    if (!save_ || !stop_ || !current_) throw std::invalid_argument("Replacement lifecycle requires save, stop and identity callbacks");
}
bool ReplacementLifecycle::InUse(const SlotRegistry& slots, const std::string& runtimeId) {
    for (const auto& slot : slots.Slots())
        if (slot.runtimeInstanceId == runtimeId && IsBusy(slot.state)) return true;
    return false;
}
void ReplacementLifecycle::BeginSave(Retirement& item) {
    item.error.clear();
    try {
        item.saved = save_(item.instance);
        if (!item.saved.valid()) throw std::runtime_error("Save barrier returned no completion");
        if (item.saved.wait_for(std::chrono::seconds(0)) == std::future_status::deferred)
            throw std::runtime_error("Save barrier must run asynchronously, not deferred on the Controller thread");
    } catch (const std::exception& error) { item.error = error.what(); }
}
void ReplacementLifecycle::Commit(SlotRegistry& slots, const std::string& slotId,
                                  const Instance& next, const std::string& type) {
    const auto oldSlot = slots.Get(slotId);
    // Assignment validates ownership/main-slot rules before any save or shutdown.
    slots.Assign(slotId, next.entryId, type, next.runtimeId);
    if (oldSlot.runtimeInstanceId.empty() || oldSlot.runtimeInstanceId == next.runtimeId ||
        !IsActive(oldSlot.state) || InUse(slots, oldSlot.runtimeInstanceId)) return;
    auto [it, inserted] = pending_.try_emplace(oldSlot.runtimeInstanceId,
        Retirement{{oldSlot.entryId, oldSlot.runtimeInstanceId}, {}, {}});
    if (inserted) BeginSave(it->second);
}
void ReplacementLifecycle::Poll(const SlotRegistry& slots) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        auto& item = it->second;
        // Do not destroy a future while its async producer is running: doing so
        // can block the Controller thread. Recheck ownership after completion.
        if (!item.error.empty() || !item.saved.valid() ||
            item.saved.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
        try {
            item.saved.get();
            if (current_(item.instance) && !InUse(slots, item.instance.runtimeId)) stop_(item.instance);
            it = pending_.erase(it);
        } catch (const std::exception& error) { item.error = error.what(); ++it; }
    }
}
void ReplacementLifecycle::Retry(const std::string& runtimeId) {
    auto found = pending_.find(runtimeId);
    if (found == pending_.end()) throw std::invalid_argument("No retired instance awaiting save");
    if (found->second.error.empty()) throw std::logic_error("Retirement is already in progress");
    BeginSave(found->second);
}
void ReplacementLifecycle::RetireUnused(const SlotRegistry& slots,const Instance& instance) {
    if(instance.runtimeId.empty() || InUse(slots,instance.runtimeId) || !current_(instance))return;
    auto [it,inserted]=pending_.try_emplace(instance.runtimeId,Retirement{instance,{},{}});
    if(inserted)BeginSave(it->second);
}
std::string ReplacementLifecycle::Error(const std::string& runtimeId) const {
    const auto found = pending_.find(runtimeId);
    return found == pending_.end() ? std::string{} : found->second.error;
}
}
