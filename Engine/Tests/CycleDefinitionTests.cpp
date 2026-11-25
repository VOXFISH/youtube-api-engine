#include "Streaming/CycleDefinition.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace Rice::Streaming;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
CycleActionDefinition Action(std::string id, float delay = 0) {
    CycleActionDefinition action; action.actionId = std::move(id); action.functionId = "native";
    action.name = action.actionId; action.timeFrom = delay; return action;
}
void OverridesAndScriptReload() {
    CycleDefinition definition;
    definition.SetScriptBaseline(25, {Action("first", 1), Action("second", 2)});
    CycleActionOverride edited; edited.timeFrom = 3.125f; edited.isCycleBlocked = true;
    definition.Patch("first", edited);
    auto changed = Action("first", 8); changed.name = "new script name"; changed.isStoppedByCycle = true;
    definition.SetScriptBaseline(30, {changed, Action("second", 4)});
    auto effective = definition.EffectiveActions();
    Check(effective[0].timeFrom == 3.125f && effective[0].isCycleBlocked && effective[0].name == "new script name" &&
        effective[0].isStoppedByCycle, "Only inspector-overridden fields survive script reload");
    Check(definition.CycleTime() == 30, "Unchanged cycle length follows script");
    definition.SetCycleTime(10); definition.SetScriptBaseline(40, {changed, Action("second", 4)});
    Check(definition.CycleTime() == 10, "Inspector cycle length takes precedence");
    definition.Move("second", 0);
    Check(definition.EffectiveActions()[0].actionId == "second", "Stable IDs survive reorder");
    definition.Remove("first");
    Check(definition.EffectiveActions().size() == 1, "Removing a script action records an override");
    definition.ResetOverride("first");
    Check(definition.EffectiveActions().size() == 2, "Restoring script values revives a removed baseline row");
    definition.Patch("first", edited);
    definition.SetScriptBaseline(40, {Action("second")});
    Check(definition.OrphanedOverrides() == std::vector<std::string>{"first"} && definition.EffectiveActions().size() == 1,
        "Missing source action retains inert override instead of attaching it to another index");
}
void PersistenceAndBuild() {
    CycleDefinition definition;
    definition.SetScriptBaseline(25, {Action("first", .125f)});
    CycleActionOverride patch; patch.isActionOnNoResponse = false; patch.reference = CycleAction::from::response;
    definition.Patch("first", patch); definition.SetCycleTime(10.5);
    auto added = Action("extra", .25f); added.order = 1; definition.Add(added);
    auto serialized = definition.Serialize();
    Check(serialized.find("10500000") != std::string::npos, "Fractional seconds persist as exact integer microseconds");
    CycleDefinition loaded; loaded.Deserialize(serialized);
    Check(loaded.Serialize() == serialized && loaded.EffectiveActions()[0].timeFrom == .125f, "Definition and overrides roundtrip");
    bool rejected = false;
    try { loaded.Deserialize("{\"schema\":2}"); } catch (const std::exception&) { rejected = true; }
    Check(rejected && loaded.Serialize() == serialized, "Malformed load leaves current model intact");
    int calls = 0;
    auto timeline = loaded.Build({{"native", [&](const auto&) { ++calls; return ActionTask{}; }}});
    Check(timeline.Count() == 3 && timeline.cycleTime == 10.5 && !timeline[1].isActionOnNoResponse, "Effective metadata builds native runtime timeline");
    timeline[2].invoke({}); Check(calls == 1, "Native callback is supplied at runtime, not deserialized");
    rejected = false;
    try { loaded.Build({}); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "Unknown native function does not become arbitrary executable code");
    auto directory = std::filesystem::temp_directory_path() /
        ("RiceCycleDefinitionTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto path = directory / "Streaming" / "cycle.json";
        loaded.Save(path); loaded.SetCycleTime(12); loaded.Save(path);
        CycleDefinition fromDisk; fromDisk.Load(path);
        Check(fromDisk.CycleTime() == 12, "Atomic save replaces an existing settings file");
        auto oversized = directory / "oversized.json";
        { std::ofstream output(oversized, std::ios::binary); output.seekp(4 * 1024 * 1024); output.put(' '); }
        bool tooLarge = false;
        try { fromDisk.Load(oversized); } catch (const std::runtime_error&) { tooLarge = true; }
        Check(tooLarge && fromDisk.CycleTime() == 12, "Oversized files are rejected before unbounded allocation without losing current settings");
        std::filesystem::remove_all(directory);
    } catch (...) { std::error_code error; std::filesystem::remove_all(directory, error); throw; }
}
}
int main() {
    try {
        OverridesAndScriptReload(); PersistenceAndBuild();
        std::cout << "Cycle definition tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "Cycle definition test failed: " << error.what() << '\n'; return 1; }
}
