#include "Engine/Data/DataRoutingMacros.h"
#include <chrono>
#include <iostream>

using namespace Rice::Data;
namespace {
void Check(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
struct GameData {
    DataContext& context;
    RICE_GAME_FIELD(context, int, round, 1);
    RICE_SLOT_FIELD_ID(context, int, score, "score-v1", 5);
    RICE_SLOT_FIELD(context, glm::vec3, position, glm::vec3(0));
    RICE_SLOT_FIELD(context, std::vector<int>, items, std::vector<int>{});
    RICE_GLOBAL_FIELD(context, int, total, 100);
    explicit GameData(DataContext& value) : context(value) { score.Description("슬롯별 점수"); }
};
}
int main(int argc, char** argv) {
    try {
        auto root = std::filesystem::path(argc > 1 ? argv[1] : "TestData/DataContext") /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        MemoryStore memory;
        memory.BeginGame("test", "game", "run-1");
        SqliteStore canonical(root / "canonical.sqlite3"), fallback(root / "fallback.sqlite3");
        GlobalFallback global(fallback, "player-1");
        global.Connect(canonical, "epoch-1");
        auto slot = std::make_shared<SlotContext>("slot-a");
        DataContext context({"test", "game", "run-1"}, memory, canonical, &global, slot);
        GameData data(context);
        RouteKey globalKey{Scope::Global, Lifetime::Stored, "test", {}, {}, "total"};
        Check(!canonical.Get(globalKey), "Field declaration must not consume global first operation");
        context.RestoreLocalDefaults();
        Check(!canonical.Get(globalKey), "Local restore must not access global");
        Check(int(data.round) == 1 && int(data.score) == 5, "Default field binding");
        data.score += 3;
        data.position.x = 4.0f;
        data.items.push_back(9);
        Check(int(data.score) == 8 && float(data.position.x) == 4.0f && int(data.items[0]) == 9,
              "Native field notation persists mutations");
        auto records = canonical.ListSlot("test", "slot-a", "game");
        Check(records.size() == 3, "Slot fields registered together");
        auto score = canonical.Get({Scope::Build, Lifetime::Slot, "test", "game", "slot-a", "score-v1"});
        Check(score && score->description == "슬롯별 점수", "Source Description persisted");
        data.total = 123;
        Check(DecodeValue<int>(canonical.Get(globalKey)->value) == 123, "First global write commits supplied value");
        context.SwitchMainAtSafeBoundary("slot-b");
        Check(int(data.score) == 5 && data.items.empty(), "Main switch loads defaults for absent slot");
        data.score = 30;
        context.SwitchMainAtSafeBoundary("slot-a");
        Check(int(data.score) == 8, "Switching back restores original slot");
        auto wrong = context.SlotDefaults().front();
        wrong.key.slot = "slot-bad";
        wrong.value = EncodeValue(std::string("wrong"));
        canonical.Put(wrong, 0);
        bool rejected = false;
        try { context.SwitchMainAtSafeBoundary("slot-bad"); } catch (const DataError&) { rejected = true; }
        Check(rejected && slot->Get().mainSlot == "slot-a", "Invalid schema leaves main unchanged");
        memory.EndGame("test", "game", "run-1");
        rejected = false;
        try { (void)int(data.round); } catch (const DataError&) { rejected = true; }
        Check(rejected, "Unbound game session rejects late access");
        std::cout << "Native data declaration and binding tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
