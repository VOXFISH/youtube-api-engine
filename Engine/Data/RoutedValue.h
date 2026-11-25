#pragma once
#include "Engine/Data/DataValueCodec.h"
#include <functional>
#include <memory>
#include <mutex>

namespace Rice::Data {
// Shared by every routed slot field of one game instance. The controller changes it only at a
// safe execution boundary, after flushing/validating all fields. This class does not schedule that boundary.
class SlotContext {
    mutable std::mutex mutex_;
    std::string mainSlot_;
    uint64_t generation_ = 1;
public:
    explicit SlotContext(std::string mainSlot) : mainSlot_(std::move(mainSlot)) {
        if (mainSlot_.empty()) throw DataError(ErrorCode::InvalidKey, "Main slot is required");
    }
    struct Snapshot { std::string mainSlot; uint64_t generation; };
    Snapshot Get() const { std::lock_guard lock(mutex_); return {mainSlot_, generation_}; }
    void SwitchAtSafeBoundary(std::string nextSlot) {
        if (nextSlot.empty()) throw DataError(ErrorCode::InvalidKey, "Main slot is required");
        std::lock_guard lock(mutex_);
        if (nextSlot != mainSlot_) {
            if (generation_ == UINT64_MAX) throw DataError(ErrorCode::InvalidValue, "Slot generation exhausted");
            mainSlot_ = std::move(nextSlot); ++generation_;
        }
    }
    void SwitchAtSafeBoundary(std::string nextSlot, IDataStore& store, std::vector<DataRecord> fields) {
        if (nextSlot.empty()) throw DataError(ErrorCode::InvalidKey, "Main slot is required");
        std::lock_guard lock(mutex_);
        if (nextSlot == mainSlot_) return;
        if (generation_ == UINT64_MAX) throw DataError(ErrorCode::InvalidValue, "Slot generation exhausted");
        for (auto& field : fields) {
            if (field.key.lifetime != Lifetime::Slot)
                throw DataError(ErrorCode::InvalidKey, "Slot rebind manifest contains a non-slot field");
            field.key.slot = nextSlot;
        }
        store.EnsureBatch(fields); // All target schemas/defaults succeed, or the transaction rolls back.
        mainSlot_ = std::move(nextSlot); ++generation_;
    }
};
template<Routable T> class Routed {
    IDataStore* store_ = nullptr;
    GlobalFallback* global_ = nullptr;
    DataRecord defaults_;
    std::shared_ptr<SlotContext> slot_;
    RouteKey CurrentKey() const {
        auto key = defaults_.key;
        if (slot_) key.slot = slot_->Get().mainSlot;
        return key;
    }
    DataRecord Defaults() const { auto value = defaults_; value.key = CurrentKey(); return value; }
public:
    Routed(IDataStore& store, RouteKey key, const T& defaults, std::string name = {}, std::string description = {},
           std::shared_ptr<SlotContext> slot = {})
        : store_(&store), defaults_{std::move(key), std::move(name), std::move(description), EncodeValue(defaults)}, slot_(std::move(slot)) {
        if (slot_ && defaults_.key.lifetime != Lifetime::Slot)
            throw DataError(ErrorCode::InvalidKey, "Slot context supplied for a non-slot field");
        CurrentKey().Validate();
    }
    Routed(GlobalFallback& global, RouteKey key, const T& defaults, std::string name = {}, std::string description = {})
        : global_(&global), defaults_{std::move(key), std::move(name), std::move(description), EncodeValue(defaults)} {
        if (defaults_.key.scope != Scope::Global) throw DataError(ErrorCode::InvalidKey, "Global wrapper requires global scope");
        defaults_.key.Validate();
    }
    T Get() const { auto defaults = Defaults(); return DecodeValue<T>((global_ ? global_->Read(defaults) : store_->Ensure(defaults)).value); }
    T Read() const { return Get(); }
    DataRecord DefaultRecord() const { return Defaults(); }
    void Description(std::string description) { defaults_.description = std::move(description); }
    void Set(const T& value) {
        auto defaults = Defaults(); auto candidate = defaults; candidate.value = EncodeValue(value);
        if (global_) { global_->Write(candidate); return; }
        auto current = store_->Ensure(defaults);
        // Keep operator-supplied names and descriptions on ordinary game writes.
        current.value = std::move(candidate.value); store_->Put(current, current.revision);
    }
    // The callback edits an isolated copy. Exceptions and CAS conflicts leave the stored value unchanged.
    template<class Function> void Edit(Function&& function) {
        if (global_) {
            global_->Mutate(Defaults(), [&](const EncodedValue& encoded) {
                T copy = DecodeValue<T>(encoded); std::invoke(function, copy); return EncodeValue(copy);
            });
            return;
        }
        const auto generation = slot_ ? slot_->Get().generation : 0;
        auto defaults = Defaults(); auto current = store_->Ensure(defaults);
        T copy = DecodeValue<T>(current.value); std::invoke(std::forward<Function>(function), copy);
        if (defaults.key != CurrentKey() || (slot_ && generation != slot_->Get().generation))
            throw DataError(ErrorCode::Conflict, "Main slot changed while editing data");
        current.value = EncodeValue(copy); store_->Put(current, current.revision);
    }
    template<class Function> void Mutate(Function&& function) { Edit(std::forward<Function>(function)); }
    Routed& operator=(const T& value) { Set(value); return *this; }
};
} // namespace Rice::Data
