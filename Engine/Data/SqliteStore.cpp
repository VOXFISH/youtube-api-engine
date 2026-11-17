#include "Engine/Data/SqliteStore.h"
#ifndef _WIN32
#error The system SQLite adapter currently requires Windows.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string_view>

namespace Rice::Data {
namespace {
std::shared_mutex processWrites;
bool writesSealed = false; // Protected by processWrites.
struct sqlite3;
struct sqlite3_stmt;
constexpr int ok = 0, row = 100, done = 101;
constexpr size_t maxValueSize = 16 * 1024 * 1024;
using Destructor = void(*)(void*);
struct Api {
    HMODULE module = nullptr;
    int (*open)(const char*, sqlite3**, int, const char*) = nullptr;
    int (*close)(sqlite3*) = nullptr;
    int (*prepare)(sqlite3*, const char*, int, sqlite3_stmt**, const char**) = nullptr;
    int (*step)(sqlite3_stmt*) = nullptr;
    int (*finalize)(sqlite3_stmt*) = nullptr;
    int (*bindText)(sqlite3_stmt*, int, const char*, int, Destructor) = nullptr;
    int (*bindBlob)(sqlite3_stmt*, int, const void*, int, Destructor) = nullptr;
    int (*bindInt)(sqlite3_stmt*, int, int64_t) = nullptr;
    const unsigned char* (*columnText)(sqlite3_stmt*, int) = nullptr;
    const void* (*columnBlob)(sqlite3_stmt*, int) = nullptr;
    int (*columnBytes)(sqlite3_stmt*, int) = nullptr;
    int64_t (*columnInt)(sqlite3_stmt*, int) = nullptr;
    const char* (*error)(sqlite3*) = nullptr;
    int (*busyTimeout)(sqlite3*, int) = nullptr;
    template<class T> void Load(T& target, const char* name) {
        target = reinterpret_cast<T>(GetProcAddress(module, name));
        if (!target) throw DataError(ErrorCode::Storage, std::string("System SQLite symbol unavailable: ") + name);
    }
    Api() {
        // Restrict loading to the trusted OS directory; never search the working directory.
        module = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw DataError(ErrorCode::Storage, "Windows system SQLite unavailable");
        try {
            Load(open, "sqlite3_open_v2"); Load(close, "sqlite3_close_v2");
            Load(prepare, "sqlite3_prepare_v2"); Load(step, "sqlite3_step");
            Load(finalize, "sqlite3_finalize"); Load(bindText, "sqlite3_bind_text");
            Load(bindBlob, "sqlite3_bind_blob"); Load(bindInt, "sqlite3_bind_int64");
            Load(columnText, "sqlite3_column_text"); Load(columnBlob, "sqlite3_column_blob");
            Load(columnBytes, "sqlite3_column_bytes"); Load(columnInt, "sqlite3_column_int64");
            Load(error, "sqlite3_errmsg"); Load(busyTimeout, "sqlite3_busy_timeout");
        } catch (...) { FreeLibrary(module); module = nullptr; throw; }
    }
    ~Api() { if (module) FreeLibrary(module); }
};
Api& api() { static Api instance; return instance; }
void Check(sqlite3* db, int result) {
    if (result != ok) throw DataError(ErrorCode::Storage, api().error(db));
}
struct Statement {
    sqlite3* db;
    sqlite3_stmt* statement = nullptr;
    Statement(sqlite3* connection, const char* sql) : db(connection) {
        Check(db, api().prepare(db, sql, -1, &statement, nullptr));
    }
    ~Statement() { if (statement) api().finalize(statement); }
    void Text(int index, std::string_view value) {
        if (value.size() > maxValueSize) throw DataError(ErrorCode::InvalidValue, "Data text exceeds size limit");
        Check(db, api().bindText(statement, index, value.data(), static_cast<int>(value.size()), reinterpret_cast<Destructor>(-1)));
    }
    void Blob(int index, const std::vector<uint8_t>& value) {
        if (value.size() > maxValueSize) throw DataError(ErrorCode::InvalidValue, "Data blob exceeds size limit");
        // SQLite treats a null address as NULL even when length is zero.
        static constexpr unsigned char empty = 0;
        Check(db, api().bindBlob(statement, index, value.empty() ? &empty : value.data(),
                               static_cast<int>(value.size()), reinterpret_cast<Destructor>(-1)));
    }
    void Int(int index, int64_t value) { Check(db, api().bindInt(statement, index, value)); }
    bool Next() {
        int result = api().step(statement);
        if (result == row) return true;
        if (result == done) return false;
        throw DataError(ErrorCode::Storage, api().error(db));
    }
    std::string Text(int index) {
        const auto* bytes = api().columnText(statement, index);
        int size = api().columnBytes(statement, index);
        return bytes ? std::string(reinterpret_cast<const char*>(bytes), size) : std::string{};
    }
    std::vector<uint8_t> Blob(int index) {
        const auto* bytes = static_cast<const uint8_t*>(api().columnBlob(statement, index));
        int size = api().columnBytes(statement, index);
        if (size < 0 || static_cast<size_t>(size) > maxValueSize)
            throw DataError(ErrorCode::InvalidValue, "Stored blob exceeds size limit");
        return size ? std::vector<uint8_t>(bytes, bytes + size) : std::vector<uint8_t>{};
    }
    int64_t Int(int index) { return api().columnInt(statement, index); }
};
void Exec(sqlite3* db, const char* sql) { Statement statement(db, sql); while (statement.Next()) {} }
struct Transaction {
    std::shared_lock<std::shared_mutex> admission;
    sqlite3* db; bool committed = false;
    explicit Transaction(sqlite3* value) : admission(processWrites), db(value) {
        if (writesSealed) throw DataError(ErrorCode::Storage,"Player data writes are sealed for retirement");
        Exec(db, "BEGIN IMMEDIATE");
    }
    ~Transaction() { if (!committed) { try { Exec(db, "ROLLBACK"); } catch (...) {} } }
    void Commit() { Exec(db, "COMMIT"); committed = true; }
};
void Part(std::string& key, std::string_view part) {
    key += std::to_string(part.size()); key += ':'; key.append(part);
}
std::string DecisionKey(const std::string& client, const std::string& epoch, const RouteKey& key) {
    if (client.empty() || epoch.empty() || client.size() > 256 || epoch.size() > 256)
        throw DataError(ErrorCode::InvalidKey, "Reconnect requires bounded stable client/epoch IDs");
    std::string result; Part(result, client); Part(result, epoch); Part(result, key.Encode()); return result;
}
void ValidateValue(const DataRecord& record) {
    record.key.Validate();
    if (record.value.type.empty() || record.value.type.size() > 1024 || !record.value.codecVersion ||
        record.value.bytes.size() > maxValueSize || record.name.size() > 4096 || record.description.size() > 65536)
        throw DataError(ErrorCode::InvalidValue, "Invalid typed data record");
}
void CheckSchema(const DataRecord& previous, const DataRecord& candidate) {
    if (previous.value.type != candidate.value.type || previous.value.codecVersion != candidate.value.codecVersion)
        throw DataError(ErrorCode::SchemaMismatch, "Data type/codec changed without migration");
}
std::vector<uint8_t> RequestFingerprint(const DataRecord& record, bool write) {
    std::string result = write ? "write" : "read";
    Part(result, record.value.type); Part(result, std::to_string(record.value.codecVersion));
    if (write) {
        Part(result, record.name); Part(result, record.description);
        Part(result, std::string_view(reinterpret_cast<const char*>(record.value.bytes.data()), record.value.bytes.size()));
    }
    return {result.begin(), result.end()};
}
}

void RouteKey::Validate() const {
    if (scope != Scope::Build && scope != Scope::Global)
        throw DataError(ErrorCode::InvalidKey, "Invalid data scope");
    if (scope == Scope::Global && (lifetime == Lifetime::Game || lifetime == Lifetime::Slot))
        throw DataError(ErrorCode::InvalidKey, dataId + " : global variables can't be game(stream) temporary.");
    if (lifetime != Lifetime::Game && lifetime != Lifetime::Slot && lifetime != Lifetime::Stored)
        throw DataError(ErrorCode::InvalidKey, "Invalid data lifetime");
    if (domain.empty() || dataId.empty() || objectKey.empty() || domain.size() > 256 ||
        dataId.size() > 256 || objectKey.size() > 1024 || build.size() > 256 || slot.size() > 256 || gameSessionId.size() > 256)
        throw DataError(ErrorCode::InvalidKey, "Missing or oversized stable data identity");
    if (scope == Scope::Global) {
        if (lifetime != Lifetime::Stored)
            throw DataError(ErrorCode::InvalidKey, dataId + " : global variables can't be game(stream) temporary.");
        if (!build.empty() || !slot.empty())
            throw DataError(ErrorCode::InvalidKey, "Global identity must not contain build/slot");
    } else if (build.empty() || (lifetime == Lifetime::Slot && slot.empty()) ||
               (lifetime != Lifetime::Slot && !slot.empty()))
        throw DataError(ErrorCode::InvalidKey, "Invalid build/slot identity");
    if ((lifetime == Lifetime::Game) != !gameSessionId.empty())
        throw DataError(ErrorCode::InvalidKey, "Only game data requires a game session ID");
}
std::string RouteKey::Encode() const {
    Validate();
    std::string result = scope == Scope::Global ? "g" : (lifetime == Lifetime::Slot ? "s" : (lifetime == Lifetime::Game ? "m" : "b"));
    for (auto* item : {&domain, &build, &slot, &dataId, &objectKey}) Part(result, *item);
    if (lifetime == Lifetime::Game) Part(result, gameSessionId);
    return result;
}
struct SqliteStore::Impl {
    sqlite3* db = nullptr;
    std::mutex mutex;
    explicit Impl(const std::filesystem::path& path) {
        auto pathUtf8 = path.u8string();
        int opened = api().open(reinterpret_cast<const char*>(pathUtf8.c_str()), &db, 2 | 4 | 0x10000, nullptr);
        if (opened != ok) {
            std::string message = api().error(db); if (db) api().close(db); db = nullptr;
            throw DataError(ErrorCode::Storage, message);
        }
        try {
            Check(db, api().busyTimeout(db, 3000));
            { Statement version(db, "PRAGMA user_version"); version.Next();
              if (version.Int(0) > 1) throw DataError(ErrorCode::SchemaMismatch, "Unsupported data DB schema"); }
            { Statement journal(db, "PRAGMA journal_mode=WAL");
              if (!journal.Next() || journal.Text(0) != "wal")
                  throw DataError(ErrorCode::Storage, "SQLite WAL mode could not be enabled"); }
            Exec(db, "PRAGMA synchronous=FULL"); Exec(db, "PRAGMA foreign_keys=ON");
            Transaction transaction(db);
            Exec(db, "CREATE TABLE IF NOT EXISTS records ("
                     "route_key TEXT PRIMARY KEY, scope INTEGER NOT NULL, lifetime INTEGER NOT NULL,"
                     "domain TEXT NOT NULL, build TEXT NOT NULL, slot TEXT NOT NULL, data_id TEXT NOT NULL,"
                     "object_key TEXT NOT NULL, type TEXT NOT NULL, codec INTEGER NOT NULL,"
                     "value BLOB NOT NULL, name TEXT NOT NULL, description TEXT NOT NULL, revision INTEGER NOT NULL CHECK(revision>0))");
            Exec(db, "CREATE INDEX IF NOT EXISTS records_slot ON records(domain,slot,build)");
            Exec(db, "CREATE TABLE IF NOT EXISTS counter (id INTEGER PRIMARY KEY CHECK(id=1), value INTEGER NOT NULL)");
            Exec(db, "INSERT OR IGNORE INTO counter(id,value) VALUES(1,0)");
            Exec(db, "CREATE TABLE IF NOT EXISTS first_decisions ("
                     "decision_key TEXT PRIMARY KEY, request BLOB NOT NULL, type TEXT NOT NULL, codec INTEGER NOT NULL,"
                     "value BLOB NOT NULL, name TEXT NOT NULL, description TEXT NOT NULL, revision INTEGER NOT NULL)");
            Exec(db, "CREATE TABLE IF NOT EXISTS replica_sessions (decision_key TEXT PRIMARY KEY)");
            Exec(db, "PRAGMA user_version=1"); transaction.Commit();
        } catch (...) { api().close(db); db = nullptr; throw; }
    }
    ~Impl() { if (db) api().close(db); }
    static DataRecord Decode(Statement& statement) {
        DataRecord record;
        record.key.scope = static_cast<Scope>(statement.Int(0));
        record.key.lifetime = static_cast<Lifetime>(statement.Int(1));
        record.key.domain = statement.Text(2); record.key.build = statement.Text(3);
        record.key.slot = statement.Text(4); record.key.dataId = statement.Text(5);
        record.key.objectKey = statement.Text(6); record.value.type = statement.Text(7);
        record.value.codecVersion = static_cast<uint32_t>(statement.Int(8));
        record.value.bytes = statement.Blob(9); record.name = statement.Text(10);
        record.description = statement.Text(11); record.revision = statement.Int(12);
        ValidateValue(record); return record;
    }
    std::optional<DataRecord> Get(const RouteKey& key) {
        if (key.lifetime == Lifetime::Game) throw DataError(ErrorCode::InvalidKey, "Game lifetime requires MemoryStore");
        Statement statement(db, "SELECT scope,lifetime,domain,build,slot,data_id,object_key,type,codec,value,name,description,revision FROM records WHERE route_key=?");
        statement.Text(1, key.Encode());
        if (!statement.Next()) return {};
        return Decode(statement);
    }
    int64_t NextRevision() {
        Statement current(db, "SELECT value FROM counter WHERE id=1"); current.Next();
        int64_t previous = current.Int(0);
        if (previous == std::numeric_limits<int64_t>::max())
            throw DataError(ErrorCode::Storage, "Data revision exhausted");
        Statement update(db, "UPDATE counter SET value=? WHERE id=1");
        update.Int(1, previous + 1); update.Next(); return previous + 1;
    }
    void Save(const DataRecord& record) {
        if (record.key.lifetime == Lifetime::Game) throw DataError(ErrorCode::InvalidKey, "Game lifetime requires MemoryStore");
        Statement statement(db, "INSERT OR REPLACE INTO records(route_key,scope,lifetime,domain,build,slot,data_id,object_key,type,codec,value,name,description,revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        statement.Text(1, record.key.Encode()); statement.Int(2, static_cast<int>(record.key.scope));
        statement.Int(3, static_cast<int>(record.key.lifetime)); statement.Text(4, record.key.domain);
        statement.Text(5, record.key.build); statement.Text(6, record.key.slot);
        statement.Text(7, record.key.dataId); statement.Text(8, record.key.objectKey);
        statement.Text(9, record.value.type); statement.Int(10, record.value.codecVersion);
        statement.Blob(11, record.value.bytes); statement.Text(12, record.name);
        statement.Text(13, record.description); statement.Int(14, record.revision); statement.Next();
    }
    DataRecord Ensure(const DataRecord& defaults) {
        if (auto current = Get(defaults.key)) { CheckSchema(*current, defaults); return *current; }
        auto result = defaults; result.revision = NextRevision(); Save(result); return result;
    }
};
SqliteStore::SqliteStore(const std::filesystem::path& file) : impl_(std::make_unique<Impl>(file)) {}
SqliteStore::~SqliteStore() = default;
bool SqliteStore::TrySealProcessWrites() {
    std::unique_lock lock(processWrites,std::try_to_lock);
    if (!lock.owns_lock()) return false;
    writesSealed=true; return true;
}
std::optional<DataRecord> SqliteStore::Get(const RouteKey& key) {
    std::lock_guard lock(impl_->mutex); return impl_->Get(key);
}
DataRecord SqliteStore::Ensure(const DataRecord& defaults) {
    ValidateValue(defaults); std::lock_guard lock(impl_->mutex);
    // Existing reads remain WAL readers rather than unnecessarily reserving the single writer.
    if (auto current = impl_->Get(defaults.key)) { CheckSchema(*current, defaults); return *current; }
    Transaction transaction(impl_->db);
    auto result = impl_->Ensure(defaults); transaction.Commit(); return result;
}
std::vector<DataRecord> SqliteStore::EnsureBatch(const std::vector<DataRecord>& defaults) {
    for (const auto& value : defaults) ValidateValue(value);
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    std::vector<DataRecord> result; result.reserve(defaults.size());
    for (const auto& value : defaults) result.push_back(impl_->Ensure(value));
    transaction.Commit(); return result;
}
DataRecord SqliteStore::Put(const DataRecord& candidate, int64_t expectedRevision) {
    ValidateValue(candidate);
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    auto previous = impl_->Get(candidate.key);
    if ((previous ? previous->revision : 0) != expectedRevision)
        throw DataError(ErrorCode::Conflict, "Data revision conflict");
    if (previous) CheckSchema(*previous, candidate);
    auto result = candidate; result.revision = impl_->NextRevision(); impl_->Save(result);
    transaction.Commit(); return result;
}
DataRecord SqliteStore::Mutate(const DataRecord& defaults, const std::function<EncodedValue(const EncodedValue&)>& function) {
    ValidateValue(defaults); std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    auto current = impl_->Ensure(defaults); auto result = current;
    result.value = function(current.value); ValidateValue(result); CheckSchema(current, result);
    result.revision = impl_->NextRevision(); impl_->Save(result); transaction.Commit(); return result;
}
bool SqliteStore::Erase(const RouteKey& key, int64_t expectedRevision) {
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    auto previous = impl_->Get(key);
    if (!previous) { transaction.Commit(); return false; }
    if (previous->revision != expectedRevision) throw DataError(ErrorCode::Conflict, "Data revision conflict");
    Statement statement(impl_->db, "DELETE FROM records WHERE route_key=?");
    statement.Text(1, key.Encode()); statement.Next(); transaction.Commit(); return true;
}
std::vector<DataRecord> SqliteStore::ListSlot(const std::string& domain, const std::string& slot, const std::string& build) {
    std::lock_guard lock(impl_->mutex);
    Statement statement(impl_->db, "SELECT scope,lifetime,domain,build,slot,data_id,object_key,type,codec,value,name,description,revision FROM records WHERE domain=? AND slot=? AND build=? AND lifetime=1 ORDER BY name,route_key");
    statement.Text(1, domain); statement.Text(2, slot); statement.Text(3, build);
    std::vector<DataRecord> result; while (statement.Next()) result.push_back(Impl::Decode(statement));
    return result;
}
void SqliteStore::ResetSlot(const std::string& domain, const std::string& slot, const std::string& build) {
    if (domain.empty() || slot.empty() || build.empty()) throw DataError(ErrorCode::InvalidKey, "Reset requires an explicit slot and game");
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    Statement statement(impl_->db, "DELETE FROM records WHERE domain=? AND slot=? AND build=? AND lifetime=1");
    statement.Text(1, domain); statement.Text(2, slot); statement.Text(3, build); statement.Next();
    transaction.Commit();
}
std::vector<DataRecord> SqliteStore::ListSlotAll(const std::string& domain, const std::string& slot) {
    if (domain.empty() || slot.empty()) throw DataError(ErrorCode::InvalidKey, "All-games listing requires an explicit slot");
    std::lock_guard lock(impl_->mutex);
    Statement statement(impl_->db, "SELECT scope,lifetime,domain,build,slot,data_id,object_key,type,codec,value,name,description,revision FROM records WHERE domain=? AND slot=? AND scope=0 AND lifetime=1 ORDER BY build,name,route_key");
    statement.Text(1, domain); statement.Text(2, slot);
    std::vector<DataRecord> result; while (statement.Next()) result.push_back(Impl::Decode(statement)); return result;
}
void SqliteStore::ResetSlotAll(const std::string& domain, const std::string& slot) {
    if (domain.empty() || slot.empty()) throw DataError(ErrorCode::InvalidKey, "All-games reset requires an explicit slot");
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    Statement statement(impl_->db, "DELETE FROM records WHERE domain=? AND slot=? AND scope=0 AND lifetime=1");
    statement.Text(1, domain); statement.Text(2, slot); statement.Next(); transaction.Commit();
}
DataRecord SqliteStore::ResolveFirst(const std::string& client, const std::string& epoch, const DataRecord& candidate, bool writeFirst) {
    ValidateValue(candidate);
    if (client.empty() || epoch.empty() || client.size() > 256 || epoch.size() > 256 || candidate.key.scope != Scope::Global)
        throw DataError(ErrorCode::InvalidKey, "Reconnect requires global identity and client/epoch");
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    auto decisionKey = DecisionKey(client, epoch, candidate.key);
    auto fingerprint = RequestFingerprint(candidate, writeFirst);
    Statement decision(impl_->db, "SELECT request,type,codec,value,name,description,revision FROM first_decisions WHERE decision_key=?");
    decision.Text(1, decisionKey);
    if (decision.Next()) {
        if (decision.Blob(0) != fingerprint)
            throw DataError(ErrorCode::Conflict, "Reconnect retry changed its first operation/payload");
        auto result = candidate; result.value.type = decision.Text(1);
        result.value.codecVersion = static_cast<uint32_t>(decision.Int(2));
        result.value.bytes = decision.Blob(3); result.name = decision.Text(4);
        result.description = decision.Text(5); result.revision = decision.Int(6);
        transaction.Commit(); return result;
    }
    auto previous = impl_->Get(candidate.key);
    if (previous) CheckSchema(*previous, candidate);
    DataRecord result;
    if (writeFirst) { result = candidate; result.revision = impl_->NextRevision(); impl_->Save(result); }
    else result = impl_->Ensure(candidate);
    Statement insert(impl_->db, "INSERT INTO first_decisions(decision_key,request,type,codec,value,name,description,revision) VALUES(?,?,?,?,?,?,?,?)");
    insert.Text(1, decisionKey); insert.Blob(2, fingerprint); insert.Text(3, result.value.type);
    insert.Int(4, result.value.codecVersion); insert.Blob(5, result.value.bytes);
    insert.Text(6, result.name); insert.Text(7, result.description); insert.Int(8, result.revision);
    insert.Next(); transaction.Commit(); return result;
}
void SqliteStore::Mirror(const DataRecord& record) {
    ValidateValue(record);
    if (record.revision <= 0) throw DataError(ErrorCode::InvalidValue, "Replica revision must be positive");
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    if (auto previous = impl_->Get(record.key)) CheckSchema(*previous, record);
    impl_->Save(record);
    Statement counter(impl_->db, "UPDATE counter SET value=MAX(value,?) WHERE id=1");
    counter.Int(1, record.revision); counter.Next(); transaction.Commit();
}
bool SqliteStore::WasReconciled(const std::string& client, const std::string& epoch, const RouteKey& key) {
    std::lock_guard lock(impl_->mutex);
    Statement statement(impl_->db, "SELECT 1 FROM replica_sessions WHERE decision_key=?");
    statement.Text(1, DecisionKey(client, epoch, key)); return statement.Next();
}
void SqliteStore::MirrorReconciled(const DataRecord& record, const std::string& client, const std::string& epoch) {
    ValidateValue(record);
    if (record.revision <= 0) throw DataError(ErrorCode::InvalidValue, "Replica revision must be positive");
    std::lock_guard lock(impl_->mutex); Transaction transaction(impl_->db);
    if (auto previous = impl_->Get(record.key)) CheckSchema(*previous, record);
    impl_->Save(record);
    Statement counter(impl_->db, "UPDATE counter SET value=MAX(value,?) WHERE id=1");
    counter.Int(1, record.revision); counter.Next();
    Statement marker(impl_->db, "INSERT OR IGNORE INTO replica_sessions(decision_key) VALUES(?)");
    marker.Text(1, DecisionKey(client, epoch, record.key)); marker.Next(); transaction.Commit();
}
std::string SqliteStore::JournalMode() {
    std::lock_guard lock(impl_->mutex); Statement statement(impl_->db, "PRAGMA journal_mode");
    statement.Next(); return statement.Text(0);
}
struct GlobalFallback::Impl {
    SqliteStore& local; SqliteStore* authoritative = nullptr;
    std::string client, epoch; std::mutex mutex;
    Impl(SqliteStore& store, std::string id) : local(store), client(std::move(id)) {
        if (client.empty()) throw DataError(ErrorCode::InvalidKey, "Global client identity required");
    }
};
GlobalFallback::GlobalFallback(SqliteStore& local, std::string client)
    : impl_(std::make_unique<Impl>(local, std::move(client))) {}
GlobalFallback::~GlobalFallback() = default;
void GlobalFallback::Connect(SqliteStore& authoritative, std::string epoch) {
    if (epoch.empty() || &authoritative == &impl_->local)
        throw DataError(ErrorCode::InvalidKey, "Reconnect requires a new epoch and a separate authoritative DB");
    std::lock_guard lock(impl_->mutex);
    impl_->authoritative = &authoritative; impl_->epoch = std::move(epoch);
}
void GlobalFallback::Disconnect() {
    std::lock_guard lock(impl_->mutex); impl_->authoritative = nullptr;
}
DataRecord GlobalFallback::Read(const DataRecord& defaults) {
    if (defaults.key.scope != Scope::Global) throw DataError(ErrorCode::InvalidKey, "Global adapter requires global data");
    std::lock_guard lock(impl_->mutex);
    if (!impl_->authoritative) return impl_->local.Ensure(defaults);
    auto result = impl_->local.WasReconciled(impl_->client, impl_->epoch, defaults.key) ? impl_->authoritative->Ensure(defaults)
        : impl_->authoritative->ResolveFirst(impl_->client, impl_->epoch, defaults, false);
    impl_->local.MirrorReconciled(result, impl_->client, impl_->epoch); return result;
}
DataRecord GlobalFallback::Write(const DataRecord& candidate) {
    if (candidate.key.scope != Scope::Global) throw DataError(ErrorCode::InvalidKey, "Global adapter requires global data");
    std::lock_guard lock(impl_->mutex);
    if (!impl_->authoritative) {
        auto current = impl_->local.Ensure(candidate); return impl_->local.Put(candidate, current.revision);
    }
    DataRecord result;
    if (!impl_->local.WasReconciled(impl_->client, impl_->epoch, candidate.key))
        result = impl_->authoritative->ResolveFirst(impl_->client, impl_->epoch, candidate, true);
    else {
        auto current = impl_->local.Get(candidate.key);
        result = impl_->authoritative->Put(candidate, current ? current->revision : 0);
    }
    impl_->local.MirrorReconciled(result, impl_->client, impl_->epoch); return result;
}
DataRecord GlobalFallback::Mutate(const DataRecord& defaults, const std::function<EncodedValue(const EncodedValue&)>& function) {
    if (defaults.key.scope != Scope::Global) throw DataError(ErrorCode::InvalidKey, "Global adapter requires global data");
    std::lock_guard lock(impl_->mutex);
    if (!impl_->authoritative) return impl_->local.Mutate(defaults, function);
    DataRecord result;
    if (!impl_->local.WasReconciled(impl_->client, impl_->epoch, defaults.key)) {
        auto candidate = impl_->local.Ensure(defaults);
        candidate.value = function(candidate.value);
        result = impl_->authoritative->ResolveFirst(impl_->client, impl_->epoch, candidate, true);
    } else result = impl_->authoritative->Mutate(defaults, function);
    impl_->local.MirrorReconciled(result, impl_->client, impl_->epoch); return result;
}
} // namespace Rice::Data
