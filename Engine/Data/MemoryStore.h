#pragma once
#include "Engine/Data/SqliteStore.h"
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace Rice::Data {
// Explicit game lifecycle API. Pause/preview hiding must not call EndGame.
// Each game session is isolated even when BuildId and DataId are identical.
class MemoryStore final : public IDataStore {
    using Session = std::tuple<std::string, std::string, std::string>;
    std::mutex mutex_;
    std::set<Session> sessions_;
    std::set<Session> usedSessions_;
    std::map<std::string, DataRecord> values_;
    int64_t revision_ = 0;
    static Session SessionOf(const RouteKey& key) { return {key.domain, key.build, key.gameSessionId}; }
    void ValidateKey(const RouteKey& key) {
        key.Validate();
        if (key.scope != Scope::Build || key.lifetime != Lifetime::Game)
            throw DataError(ErrorCode::InvalidKey, "MemoryStore requires build game data");
        if (!sessions_.contains(SessionOf(key)))
            throw DataError(ErrorCode::InvalidKey, "Game data is not bound to an active session");
    }
    void ValidateRecord(const DataRecord& value) {
        ValidateKey(value.key);
        if (value.value.type.empty() || value.value.type.size() > 1024 || !value.value.codecVersion ||
            value.value.bytes.size() > 16 * 1024 * 1024 || value.name.size() > 4096 || value.description.size() > 65536)
            throw DataError(ErrorCode::InvalidValue, "Invalid typed game data");
    }
    static void Schema(const DataRecord& previous, const DataRecord& next) {
        if (previous.value.type != next.value.type || previous.value.codecVersion != next.value.codecVersion)
            throw DataError(ErrorCode::SchemaMismatch, "Game field schema conflict");
    }
    static int64_t Next(int64_t& revision) {
        if (revision == std::numeric_limits<int64_t>::max())
            throw DataError(ErrorCode::Storage, "Game revision exhausted");
        return ++revision;
    }
public:
    void BeginGame(std::string domain, std::string build, std::string session) {
        RouteKey probe{Scope::Build, Lifetime::Game, std::move(domain), std::move(build), "", "session", "singleton", std::move(session)};
        probe.Validate(); std::lock_guard lock(mutex_);
        auto identity = SessionOf(probe);
        if (usedSessions_.contains(identity))
            throw DataError(ErrorCode::Conflict, "Game session ID must be new for every run");
        usedSessions_.insert(identity); sessions_.insert(std::move(identity));
    }
    void EndGame(const std::string& domain, const std::string& build, const std::string& session) {
        std::lock_guard lock(mutex_); const Session target{domain, build, session};
        sessions_.erase(target);
        for (auto it = values_.begin(); it != values_.end();) {
            if (SessionOf(it->second.key) == target) it = values_.erase(it); else ++it;
        }
    }
    std::optional<DataRecord> Get(const RouteKey& key) override {
        std::lock_guard lock(mutex_); ValidateKey(key);
        auto found = values_.find(key.Encode());
        return found == values_.end() ? std::optional<DataRecord>{} : found->second;
    }
    DataRecord Ensure(const DataRecord& defaults) override {
        std::lock_guard lock(mutex_); ValidateRecord(defaults);
        auto key = defaults.key.Encode();
        if (auto found = values_.find(key); found != values_.end()) {
            Schema(found->second, defaults); return found->second;
        }
        auto result = defaults; result.revision = Next(revision_); values_.emplace(std::move(key), result); return result;
    }
    std::vector<DataRecord> EnsureBatch(const std::vector<DataRecord>& defaults) override {
        std::lock_guard lock(mutex_);
        // Stage the batch so any schema/allocation error leaves every live field unchanged.
        auto staged = values_; auto nextRevision = revision_; std::vector<DataRecord> result;
        result.reserve(defaults.size());
        for (const auto& item : defaults) {
            ValidateRecord(item); auto key = item.key.Encode();
            auto found = staged.find(key);
            if (found != staged.end()) { Schema(found->second, item); result.push_back(found->second); }
            else {
                auto candidate = item; candidate.revision = Next(nextRevision);
                staged.emplace(std::move(key), candidate); result.push_back(std::move(candidate));
            }
        }
        values_.swap(staged); revision_ = nextRevision; return result;
    }
    DataRecord Put(const DataRecord& candidate, int64_t expectedRevision) override {
        std::lock_guard lock(mutex_); ValidateRecord(candidate);
        auto key = candidate.key.Encode(); auto found = values_.find(key);
        if ((found == values_.end() ? 0 : found->second.revision) != expectedRevision)
            throw DataError(ErrorCode::Conflict, "Game data revision conflict");
        if (found != values_.end()) Schema(found->second, candidate);
        auto result = candidate; result.revision = Next(revision_); values_.insert_or_assign(std::move(key), result); return result;
    }
    DataRecord Mutate(const DataRecord& defaults, const std::function<EncodedValue(const EncodedValue&)>& function) override {
        std::lock_guard lock(mutex_); ValidateRecord(defaults);
        auto key = defaults.key.Encode(); auto found = values_.find(key);
        auto current = found == values_.end() ? defaults : found->second;
        Schema(current, defaults);
        auto result = current; result.value = function(current.value);
        ValidateRecord(result); Schema(current, result);
        result.revision = Next(revision_); values_.insert_or_assign(std::move(key), result); return result;
    }
};
} // namespace Rice::Data
