#include "Engine/Data/RoutedValue.h"
#include "Engine/Data/MemoryStore.h"
#include "Engine/Data/DataField.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>

struct TestState {
    int count = 0;
    glm::vec3 position{0};
    std::vector<std::string> names;
};
using IntList = std::vector<int>;
struct NativeState { int score = 0; glm::vec3 position{0}; IntList items; };
#define NATIVE_STATE_FIELDS(X) X(int, score) X(glm::vec3, position) X(IntList, items)
RICE_DATA_STRUCT(NativeState, "tests/native-state", 1, NATIVE_STATE_FIELDS)
namespace Rice::Data {
template<> struct DataCodec<TestState> : StructDataCodec<TestState, &TestState::count, &TestState::position, &TestState::names> {
    static std::string Type() { return "tests/state"; }
    static constexpr uint32_t Version = 1;
};
}
using namespace Rice::Data;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void Reject(ErrorCode code, Function&& function) {
    try { function(); } catch (const DataError& error) {
        Check(error.code == code, "Unexpected data error kind"); return;
    }
    throw std::runtime_error("Expected data operation to fail");
}
RouteKey Slot(std::string slot = "slot-a", std::string build = "game-a", std::string data = "score") {
    return {Scope::Build, Lifetime::Slot, "test-domain", std::move(build), std::move(slot), std::move(data), "singleton"};
}
RouteKey Global(std::string data = "shared") {
    return {Scope::Global, Lifetime::Stored, "test-domain", "", "", std::move(data), "singleton"};
}
DataRecord IntRecord(RouteKey key, int value) { return {std::move(key), "Score", "Default score", EncodeValue(value)}; }
struct TempFiles {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("rice-data-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::vector<std::filesystem::path> files;
    TempFiles() { std::filesystem::create_directory(root); }
    std::filesystem::path File(const char* name) { auto file = root / name; files.push_back(file); return file; }
    ~TempFiles() {
        std::error_code error;
        for (const auto& file : files) {
            std::filesystem::remove(file, error);
            auto wal = file; wal += "-wal"; std::filesystem::remove(wal, error);
            auto shm = file; shm += "-shm"; std::filesystem::remove(shm, error);
        }
        std::filesystem::remove(root, error);
    }
};
void Codecs() {
    static_assert(Encodable<int> && Encodable<TestState> && Encodable<glm::vec2>);
    static_assert(!Encodable<int*> && !Encodable<void*>);
    Check(DecodeValue<uint64_t>(EncodeValue(UINT64_MAX)) == UINT64_MAX, "uint64 precision");
    Check(DecodeValue<int64_t>(EncodeValue(INT64_MIN)) == INT64_MIN, "signed boundary");
    Check(DecodeValue<std::array<int, 3>>(EncodeValue(std::array<int, 3>{1, -2, 3}))[1] == -2, "array codec");
    Check(DecodeValue<std::vector<bool>>(EncodeValue(std::vector<bool>{true, false}))[0], "bool list");
    TestState state{42, {1.25f, -2.5f, 3.75f}, {"hello", "\xED\x95\x9C\xEA\xB8\x80"}};
    auto restored = DecodeValue<TestState>(EncodeValue(state));
    Check(restored.count == state.count && restored.position == state.position && restored.names == state.names, "custom struct roundtrip");
    Reject(ErrorCode::InvalidValue, [] { EncodeValue(std::numeric_limits<double>::infinity()); });
    Reject(ErrorCode::InvalidValue, [] { EncodeValue(std::string("\xc0\x80", 2)); });
    auto damaged = EncodeValue(42); damaged.bytes.pop_back();
    Reject(ErrorCode::InvalidValue, [&] { DecodeValue<int>(damaged); });
    auto wrong = EncodeValue(std::string("forty-two"));
    Reject(ErrorCode::SchemaMismatch, [&] { DecodeValue<int>(wrong); });
    Check(Slot("a/b", "c").Encode() != Slot("a", "b/c").Encode(), "Stable key encoding collision");
    auto invalid = Global(); invalid.lifetime = Lifetime::Slot;
    Reject(ErrorCode::InvalidKey, [&] { invalid.Encode(); });
}
void PersistenceAndSlots(TempFiles& temporary) {
    auto file = temporary.File("slots.sqlite3");
    int64_t savedRevision = 0;
    {
        SqliteStore store(file);
        Check(store.JournalMode() == "wal", "WAL is required");
        auto record = store.Ensure(IntRecord(Slot(), 10));
        record.value = EncodeValue(45); record.name = "Edited"; record.description = "Operator description";
        record = store.Put(record, record.revision); savedRevision = record.revision;
        Check(DecodeValue<int>(store.Ensure(IntRecord(Slot(), 999)).value) == 45, "Default replaced saved value");
        auto stale = record; stale.value = EncodeValue(50);
        Reject(ErrorCode::Conflict, [&] { store.Put(stale, savedRevision - 1); });
        stale.value = EncodeValue(std::string("bad"));
        Reject(ErrorCode::SchemaMismatch, [&] { store.Put(stale, savedRevision); });
        store.Ensure(IntRecord(Slot("slot-b"), 3));
        store.Ensure(IntRecord(Slot("slot-a", "game-b"), 4));
        auto otherDomain = Slot(); otherDomain.domain = "another-domain";
        store.Ensure(IntRecord(otherDomain, 5));
        auto values = store.ListSlot("test-domain", "slot-a", "game-a");
        Check(values.size() == 1 && values[0].description == "Operator description", "Slot list metadata");
    }
    {
        SqliteStore store(file);
        Check(store.Get(Slot())->revision == savedRevision, "Revision did not persist");
        auto context = std::make_shared<SlotContext>("slot-a");
        Routed<int> score(store, Slot(), 10, "Score", "", context);
        Check(score.Get() == 45, "Main slot A restore");
        context->SwitchAtSafeBoundary("slot-b"); Check(score.Get() == 3, "Main slot B restore");
        context->SwitchAtSafeBoundary("new-slot"); Check(score.Get() == 10, "New slot must use defaults");
        score.Set(20);
        context->SwitchAtSafeBoundary("slot-a"); Check(score.Get() == 45, "Switch must preserve previous slot");
        auto wrongTarget = IntRecord(Slot("invalid-target"), 0);
        wrongTarget.value = EncodeValue(std::string("wrong schema")); store.Ensure(wrongTarget);
        auto newField = IntRecord(Slot("slot-a", "game-a", "new-field"), 7);
        Reject(ErrorCode::SchemaMismatch, [&] {
            context->SwitchAtSafeBoundary("invalid-target", store, {newField, score.DefaultRecord()});
        });
        Check(context->Get().mainSlot == "slot-a" && !store.Get(Slot("invalid-target", "game-a", "new-field")),
              "Failed slot prevalidation must preserve context and roll back target defaults");
        Check(store.Get(Slot())->description == "Operator description", "Gameplay overwrote operator metadata");
        Reject(ErrorCode::Conflict, [&] {
            score.Edit([&](int& value) { value = 80; context->SwitchAtSafeBoundary("slot-b"); });
        });
        Check(DecodeValue<int>(store.Get(Slot())->value) == 45, "Conflicting edit was stored");
        store.ResetSlot("test-domain", "slot-a", "game-a");
        Check(!store.Get(Slot()) && store.Get(Slot("slot-a", "game-b")), "Reset crossed game boundary");
        auto recreated = store.Ensure(IntRecord(Slot(), 10));
        Check(recreated.revision > savedRevision, "Reset allowed stale revision resurrection");
        Reject(ErrorCode::Conflict, [&] { store.Erase(Slot(), savedRevision); });
        Check(store.Erase(Slot(), recreated.revision), "Delete failed");
        Check(!store.Erase(Slot(), recreated.revision), "Delete should be idempotent");
    }
}
void GameLifecycle() {
    MemoryStore store;
    RouteKey a{Scope::Build, Lifetime::Game, "domain", "game", "", "score", "singleton", "session-a"};
    RouteKey b = a; b.gameSessionId = "session-b";
    Routed<int> scoreA(store, a, 10), scoreB(store, b, 10);
    Reject(ErrorCode::InvalidKey, [&] { scoreA.Get(); });
    store.BeginGame("domain", "game", "session-a"); store.BeginGame("domain", "game", "session-b");
    scoreA.Set(23); Check(scoreA.Get() == 23 && scoreB.Get() == 10, "Game sessions share temporary data");
    Reject(ErrorCode::Conflict, [&] { store.BeginGame("domain", "game", "session-a"); });
    store.EndGame("domain", "game", "session-a");
    Reject(ErrorCode::InvalidKey, [&] { scoreA.Get(); });
    Check(scoreB.Get() == 10, "Stopping A cleared B");
    Reject(ErrorCode::Conflict, [&] { store.BeginGame("domain", "game", "session-a"); });
    a.gameSessionId = "session-c"; store.BeginGame("domain", "game", "session-c");
    Routed<int> restarted(store, a, 10);
    Check(restarted.Get() == 10, "New game did not use defaults");
    restarted.Edit([](int& value) { value += 2; }); Check(restarted.Get() == 12, "Game routed Edit failed");
    Reject(ErrorCode::InvalidKey, [&] { scoreA.Set(100); });
    store.EndGame("domain", "game", "session-c"); store.EndGame("domain", "game", "session-b");
}
void NativeFacade(TempFiles& temporary) {
    SqliteStore store(temporary.File("facade.sqlite3"));
    Field<NativeState> data(Routed<NativeState>(store, Slot("facade"), NativeState{}));
    data.Description("Native proxy example");
    data.score = 3; ++data.score; data.score += 6;
    data.position.x = 1.5f; data.position.y += 2.25f;
    data.items.push_back(10); data.items.push_back(20); data.items[1] += 5;
    auto element = data.items[0]; element = 15;
    Check(int(data.score) == 10 && float(data.position.x) == 1.5f && float(data.position.y) == 2.25f,
          "Named native scalar/vector proxies failed");
    Check(data.items.size() == 2 && int(data.items[0]) == 15 && int(data.items[1]) == 25, "Native list proxy failed");
    data.items.insert(1, 17); data.items.erase(0); data.items.pop_back();
    Check(data.items.size() == 1 && int(data.items[0]) == 17, "Native list mutation failed");
    auto persisted = DecodeValue<NativeState>(store.Get(Slot("facade"))->value);
    Check(persisted.score == 10 && persisted.items == IntList{17}, "Facade bypassed persistence");
    Check(store.Get(Slot("facade"))->description == "Native proxy example", "Native description metadata failed");
    Field<std::array<int, 2>> array(std::array<int, 2>{1, 2}); array[1] = 8; Check(int(array[1]) == 8, "Array proxy failed");
    Field<std::list<int>> linked(std::list<int>{1}); linked.push_back(2); linked[0] += 4;
    Check(int(linked[0]) == 5 && linked.size() == 2, "Linked list proxy failed");
    Field<int> count(Routed<int>(store, Slot("facade", "game-a", "count"), INT_MAX));
    Reject(ErrorCode::InvalidValue, [&] { ++count; }); Check(int(count) == INT_MAX, "Overflow changed stored scalar");
    Reject(ErrorCode::InvalidValue, [&] { data.items[99] = 1; });
}
void Reconnect(TempFiles& temporary) {
    auto authorityPath = temporary.File("authority.sqlite3"), localPath = temporary.File("fallback.sqlite3");
    SqliteStore authority(authorityPath), local(localPath);
    GlobalFallback router(local, "player-a");
    Routed<int> shared(router, Global(), 10);
    Check(shared.Get() == 10, "Cold fallback default");
    shared.Set(20);
    authority.Ensure(IntRecord(Global(), 30));
    router.Connect(authority, "epoch-read");
    Check(shared.Get() == 30, "Read-first must replace local with authority");
    router.Disconnect(); shared.Set(20);
    auto remote = authority.Get(Global()); remote->value = EncodeValue(35); authority.Put(*remote, remote->revision);
    router.Connect(authority, "epoch-write"); shared.Set(40);
    Check(DecodeValue<int>(authority.Get(Global())->value) == 40, "Write-first did not override authority");
    router.Disconnect(); shared.Set(20);
    router.Connect(authority, "epoch-increment"); shared.Edit([](int& value) { ++value; });
    Check(shared.Get() == 21, "First increment must use local baseline");
    // Every subsequent getter sees the authoritative SQL value, not a player snapshot.
    remote = authority.Get(Global()); remote->value = EncodeValue(90); authority.Put(*remote, remote->revision);
    Check(shared.Get() == 90, "Global getter returned stale cache");
    // Local applied markers survive the adapter's own lifetime.
    {
        GlobalFallback recreated(local, "player-a"); recreated.Connect(authority, "epoch-increment");
        Check(DecodeValue<int>(recreated.Read(IntRecord(Global(), 10)).value) == 90, "Resolved epoch was not durable");
    }
    // A reply-loss retry replays exactly the response originally committed, even after later writers.
    auto first = authority.ResolveFirst("retry-player", "retry-epoch", IntRecord(Global("retry"), 7), true);
    auto later = first; later.value = EncodeValue(8); authority.Put(later, first.revision);
    {
        SqliteStore reopened(authorityPath);
        auto replay = reopened.ResolveFirst("retry-player", "retry-epoch", IntRecord(Global("retry"), 7), true);
        Check(replay.revision == first.revision && DecodeValue<int>(replay.value) == 7, "First operation was applied twice");
        Reject(ErrorCode::Conflict, [&] { reopened.ResolveFirst("retry-player", "retry-epoch", IntRecord(Global("retry"), 9), true); });
    }
    // Per-key first-operation state is independent.
    local.Ensure(IntRecord(Global("other"), 4)); authority.Ensure(IntRecord(Global("other"), 5));
    router.Write(IntRecord(Global("other"), 6));
    Check(DecodeValue<int>(authority.Get(Global("other"))->value) == 6, "First operation was not per key");
}
void ConcurrentTransactions(TempFiles& temporary) {
    auto file = temporary.File("concurrency.sqlite3");
    SqliteStore first(file), second(file);
    auto defaults = IntRecord(Global("counter"), 0); first.Ensure(defaults);
    std::atomic<bool> failed = false;
    auto increment = [&](SqliteStore& store) {
        try {
            for (int i = 0; i < 30; ++i) store.Mutate(defaults, [](const EncodedValue& encoded) {
                return EncodeValue(DecodeValue<int>(encoded) + 1);
            });
        } catch (...) { failed = true; }
    };
    std::thread a(increment, std::ref(first)), b(increment, std::ref(second));
    a.join(); b.join();
    Check(!failed && DecodeValue<int>(first.Get(defaults.key)->value) == 60, "SQLite transaction lost concurrent increments");
    auto before = first.Get(defaults.key);
    try { first.Mutate(defaults, [](const EncodedValue&) -> EncodedValue { throw std::runtime_error("cancel"); }); }
    catch (const std::runtime_error&) {}
    Check(first.Get(defaults.key)->revision == before->revision, "Failed transaction was not rolled back");
}
void WholeSlotReset(TempFiles& temporary) {
    SqliteStore store(temporary.File("all-games.sqlite3"));
    auto a=Slot("one-slot","game-a"), b=Slot("one-slot","game-b"), other=Slot("other-slot","game-a");
    auto separateDomain=a;separateDomain.domain="another-domain";
    auto stored=a;stored.slot.clear();stored.lifetime=Lifetime::Stored;
    auto global=Global();
    for(const auto& key:{a,b,other,separateDomain,stored,global})store.Ensure(IntRecord(key,5));
    Check(store.ListSlotAll("test-domain","one-slot").size()==2,"All-games listing crossed scope");
    Reject(ErrorCode::InvalidKey,[&]{store.ResetSlotAll("test-domain","");});
    store.ResetSlotAll("test-domain","one-slot");
    Check(!store.Get(a)&&!store.Get(b),"Whole slot reset missed a game");
    Check(store.Get(other)&&store.Get(separateDomain)&&store.Get(stored)&&store.Get(global),"Whole slot reset erased another scope");
}
}
int main() {
    try {
        TempFiles temporary; Codecs(); GameLifecycle(); NativeFacade(temporary); PersistenceAndSlots(temporary); Reconnect(temporary); ConcurrentTransactions(temporary); WholeSlotReset(temporary);
        std::cout << "SQLite persistence, typed codecs, slot routing, CAS, reconnect and transaction tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
