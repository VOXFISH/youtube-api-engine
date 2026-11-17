#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Rice::Data {
enum class Scope { Build, Global };
enum class Lifetime { Game, Slot, Stored };
enum class ErrorCode { InvalidKey, Conflict, SchemaMismatch, Storage, InvalidValue };
class DataError : public std::runtime_error {
public:
    ErrorCode code;
    DataError(ErrorCode value, const std::string& message) : std::runtime_error(message), code(value) {}
};
struct RouteKey {
    Scope scope = Scope::Build;
    Lifetime lifetime = Lifetime::Stored;
    std::string domain, build, slot, dataId, objectKey = "singleton";
    std::string gameSessionId;
    std::string Encode() const;
    void Validate() const;
    bool operator==(const RouteKey&) const = default;
};
struct EncodedValue {
    std::string type;
    uint32_t codecVersion = 1;
    std::vector<uint8_t> bytes;
    bool operator==(const EncodedValue&) const = default;
};
struct DataRecord {
    RouteKey key;
    std::string name, description;
    EncodedValue value;
    int64_t revision = 0;
};

class IDataStore {
public:
    virtual ~IDataStore() = default;
    virtual std::optional<DataRecord> Get(const RouteKey&) = 0;
    virtual DataRecord Ensure(const DataRecord&) = 0;
    virtual std::vector<DataRecord> EnsureBatch(const std::vector<DataRecord>&) = 0;
    virtual DataRecord Put(const DataRecord&, int64_t expectedRevision) = 0;
    virtual DataRecord Mutate(const DataRecord&, const std::function<EncodedValue(const EncodedValue&)>&) = 0;
};

// Synchronous storage boundary. Call on the data/script worker, never on rendering/audio threads.
// Windows uses only the OS SQLite from System32; no database server or downloaded DLL is needed.
class SqliteStore : public IDataStore {
public:
    explicit SqliteStore(const std::filesystem::path& file);
    ~SqliteStore();
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;
    std::optional<DataRecord> Get(const RouteKey& key) override;
    DataRecord Ensure(const DataRecord& defaults) override;
    std::vector<DataRecord> EnsureBatch(const std::vector<DataRecord>& defaults) override;
    // expectedRevision=0 means insert only. Other values are compare-and-swap.
    DataRecord Put(const DataRecord& candidate, int64_t expectedRevision) override;
    // The callback executes under the transaction lock: it must be pure, bounded, and not reenter a store.
    DataRecord Mutate(const DataRecord& defaults, const std::function<EncodedValue(const EncodedValue&)>& function) override;
    bool Erase(const RouteKey& key, int64_t expectedRevision);
    std::vector<DataRecord> ListSlot(const std::string& domain, const std::string& slot,
                                     const std::string& build);
    void ResetSlot(const std::string& domain, const std::string& slot, const std::string& build);
    // Explicit all-games operations are still limited to one slot and never include global/stored rows.
    std::vector<DataRecord> ListSlotAll(const std::string& domain, const std::string& slot);
    void ResetSlotAll(const std::string& domain, const std::string& slot);
    // Resolve exactly one first operation per client/epoch/key. Retries must carry the same payload.
    // The decision and response survive reopening the DB. First-write deliberately bypasses CAS.
    DataRecord ResolveFirst(const std::string& client, const std::string& epoch,
                            const DataRecord& candidate, bool writeFirst);
    // Replica overwrite is explicit, and does not claim a new authoritative revision.
    void Mirror(const DataRecord& record);
    bool WasReconciled(const std::string& client, const std::string& epoch, const RouteKey& key);
    void MirrorReconciled(const DataRecord& record, const std::string& client, const std::string& epoch);
    std::string JournalMode();
    // Player retirement only: seal this process against new transactions after
    // all existing FULL-synchronous commits/rollbacks finish. Nonblocking poll.
    static bool TrySealProcessWrites();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// In-process adapter for an authoritative store and a separate durable fallback store.
// IPC transport/recovery of UnknownCommit requests is a separate integration layer.
class GlobalFallback {
public:
    GlobalFallback(SqliteStore& local, std::string client);
    ~GlobalFallback();
    void Connect(SqliteStore& authoritative, std::string epoch);
    void Disconnect();
    DataRecord Read(const DataRecord& defaults);
    DataRecord Write(const DataRecord& candidate);
    DataRecord Mutate(const DataRecord& defaults, const std::function<EncodedValue(const EncodedValue&)>& function);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Rice::Data
