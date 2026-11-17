#pragma once
#include "Engine/Data/DataField.h"
#include "Engine/Data/MemoryStore.h"
#include <set>
#include <map>

namespace Rice::Data {
enum class AccessKind { Locked, Open, Policy };
// Metadata for the future direct streaming-write endpoint. Internal game calls and
// local Controller operators are separate origins and do not use this policy.
struct AccessPolicy {
    AccessKind kind = AccessKind::Locked;
    std::string policyId;
    static AccessPolicy Locked() { return {}; }
    static AccessPolicy Open() { return {AccessKind::Open, {}}; }
    static AccessPolicy Named(std::string id) {
        if (id.empty() || id.size() > 256)
            throw DataError(ErrorCode::InvalidValue, "Access policy requires a bounded stable ID");
        return {AccessKind::Policy, std::move(id)};
    }
};
struct DataIdentity {
    std::string domain, build, gameSession, objectKey = "singleton";
};
struct FieldDescriptor {
    AccessPolicy access;
    std::function<DataRecord()> defaults;
};

// One context belongs to one data-only object. Backends outlive the context and
// its fields. The game owner explicitly begins/ends the MemoryStore session.
// Construction does not read or write global SQL, preserving first-operation recovery.
class DataContext {
    inline static std::mutex activeMutex_;
    inline static std::set<DataContext*> active_;
    inline static std::string initialMainSlot_;
    DataIdentity identity_;
    MemoryStore& game_;
    SqliteStore& durable_;
    GlobalFallback* global_;
    std::shared_ptr<SlotContext> slot_;
    std::vector<FieldDescriptor> fields_;
    std::set<std::string> identities_;
public:
    DataContext(DataIdentity identity, MemoryStore& game, SqliteStore& durable,
                GlobalFallback* global = nullptr, std::shared_ptr<SlotContext> slot = {})
        : identity_(std::move(identity)), game_(game), durable_(durable),
          global_(global), slot_(std::move(slot)) {
        RouteKey probe{Scope::Build, Lifetime::Stored, identity_.domain, identity_.build,
                       {}, "context", identity_.objectKey};
        probe.Validate();
        std::lock_guard lock(activeMutex_);
        if(!initialMainSlot_.empty()){
            if(slot_ && slot_->Get().mainSlot!=initialMainSlot_)throw DataError(ErrorCode::InvalidKey,"DataContext main slot differs from Controller launch slot");
            if(!slot_)slot_=std::make_shared<SlotContext>(initialMainSlot_);
        }
        active_.insert(this);
    }
    ~DataContext(){std::lock_guard lock(activeMutex_);active_.erase(this);}
    DataContext(const DataContext&) = delete;
    DataContext& operator=(const DataContext&) = delete;
    static void ConfigurePlayerMainSlot(std::string slot){
        if(slot.empty()||slot.size()>256)throw DataError(ErrorCode::InvalidKey,"Invalid initial Player main slot");
        std::lock_guard lock(activeMutex_);
        if(!active_.empty())throw DataError(ErrorCode::Conflict,"Configure main slot before creating game data");
        initialMainSlot_=std::move(slot);
    }
    static std::string PlayerMainSlot(){std::lock_guard lock(activeMutex_);return initialMainSlot_;}

    // Player owner-thread safe boundary. Validate/restore all registered slot
    // fields before publishing any new slot binding. Global first-operation
    // recovery and game-memory values are intentionally untouched.
    static void RestorePlayerSlot(const std::string& mainSlot){
        if(mainSlot.empty()||mainSlot.size()>256)throw DataError(ErrorCode::InvalidKey,"Invalid Player main slot");
        std::lock_guard lock(activeMutex_);
        std::map<SqliteStore*,std::vector<DataRecord>> batches;
        std::set<std::shared_ptr<SlotContext>> bindings;
        for(auto* context:active_)if(context->slot_){
            auto snapshot=context->slot_->Get();
            if(snapshot.mainSlot!=mainSlot && snapshot.generation==UINT64_MAX)
                throw DataError(ErrorCode::InvalidValue,"Slot generation exhausted");
            auto fields=context->SlotDefaults();
            for(auto& field:fields){field.key.slot=mainSlot;batches[&context->durable_].push_back(std::move(field));}
            bindings.insert(context->slot_);
        }
        for(auto& [store,fields]:batches)store->EnsureBatch(fields);
        for(const auto& binding:bindings)binding->SwitchAtSafeBoundary(mainSlot);
    }

    template<class T, Scope S = Scope::Build,
             Lifetime L = (S == Scope::Global ? Lifetime::Stored : Lifetime::Game)>
    Field<T> Bind(std::string stableId, T initial, std::string name,
                  AccessPolicy access = {}, std::string description = {}) {
        static_assert(Encodable<T>, "Routed data type requires a DataCodec or RICE_DATA_STRUCT registration");
        static_assert(S != Scope::Global || L == Lifetime::Stored,
                      "global variables can't be game(stream) temporary. slot is also temporary for this rule.");
        if (access.kind != AccessKind::Locked && access.kind != AccessKind::Open && access.kind != AccessKind::Policy)
            throw DataError(ErrorCode::InvalidValue, "Unknown access policy");
        if (access.kind == AccessKind::Policy && access.policyId.empty())
            throw DataError(ErrorCode::InvalidValue, "Missing access policy ID");
        RouteKey key{S, L, identity_.domain, S == Scope::Global ? "" : identity_.build,
                     {}, std::move(stableId), identity_.objectKey};
        if constexpr (L == Lifetime::Game) key.gameSessionId = identity_.gameSession;
        if constexpr (L == Lifetime::Slot) {
            if (!slot_) throw DataError(ErrorCode::InvalidKey, "Slot data requires a main slot");
            key.slot = slot_->Get().mainSlot;
        }
        key.Validate();
        const auto identity = key.Encode();
        if (identities_.contains(identity)) throw DataError(ErrorCode::Conflict, "Duplicate field data ID");
        auto bind = [&]() -> Routed<T> {
            if constexpr (S == Scope::Global) {
                if (!global_) throw DataError(ErrorCode::InvalidKey, "Global data requires a fallback service");
                return Routed<T>(*global_, key, initial, name, description);
            } else if constexpr (L == Lifetime::Game) {
                return Routed<T>(game_, key, initial, name, description);
            } else {
                return Routed<T>(durable_, key, initial, name, description,
                                 L == Lifetime::Slot ? slot_ : std::shared_ptr<SlotContext>{});
            }
        };
        Field<T> field(bind());
        // Capture the proxy handle, not an address into a movable data object.
        FieldDescriptor descriptor{std::move(access), [field]() { return field.DefaultRecord(); }};
        auto [position, inserted] = identities_.insert(identity);
        try { fields_.push_back(std::move(descriptor)); }
        catch (...) { if (inserted) identities_.erase(position); throw; }
        return field;
    }
    const std::vector<FieldDescriptor>& Fields() const noexcept { return fields_; }
    std::vector<DataRecord> SlotDefaults() const {
        std::vector<DataRecord> result;
        for (const auto& field : fields_) {
            auto record = field.defaults();
            if (record.key.lifetime == Lifetime::Slot) result.push_back(std::move(record));
        }
        return result;
    }
    // Call before Start on the owner thread; global values remain untouched.
    void RestoreLocalDefaults() {
        std::vector<DataRecord> game, durable;
        for (const auto& field : fields_) {
            auto record = field.defaults();
            if (record.key.scope == Scope::Global) continue;
            (record.key.lifetime == Lifetime::Game ? game : durable).push_back(std::move(record));
        }
        if (!durable.empty()) durable_.EnsureBatch(durable);
        if (!game.empty()) game_.EnsureBatch(game);
    }
    // For several data objects, aggregate all SlotDefaults and use their shared
    // SlotContext once. This convenience method applies to a single data object.
    void SwitchMainAtSafeBoundary(std::string nextSlot) {
        if (!slot_) throw DataError(ErrorCode::InvalidKey, "No main slot binding");
        slot_->SwitchAtSafeBoundary(std::move(nextSlot), durable_, SlotDefaults());
    }
};
} // namespace Rice::Data
