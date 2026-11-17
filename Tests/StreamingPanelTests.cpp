#include "Editor/StreamingPanel.h"
#include <imgui_internal.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace Rice;
using namespace Rice::Streaming;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct ControlledScript : Component { int total = 0; void Add(int amount) { total += amount; } };
void Register(YouTubeListener& listener) {
    listener.BindNative<&ControlledScript::Add>("add", "ControlledScript.Add", "!add {amount}", std::tuple{Parameter<int>("amount")});
}
struct Fixture {
    Scene scene;
    StreamingPanel panel;
    EntityID objectId = 0;
    std::filesystem::path root;
    std::string text;
    int actionCalls = 0;
    Fixture() {
        root = std::filesystem::temp_directory_path() /
            ("RiceStreamingPanelTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto& object = scene.CreateGameObject("Audience controlled"); objectId = object.GetID();
        object.AddComponent<ControlledScript>(); Register(object.AddComponent<YouTubeListener>());
        panel.commandsOpen = panel.cycleOpen = true;
        panel.RegisterCycleFunction("NativeAction", [&](const auto&) { ++actionCalls; return ActionTask{}; });
        CycleActionDefinition action; action.actionId = "native-action"; action.functionId = "NativeAction"; action.name = "Native action";
        panel.CycleModel().SetScriptBaseline(10, {action});
        ImGui::CreateContext(); auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.DisplaySize = {1600, 1000}; io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels = nullptr; int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Fixture() {
        panel.StopPreview(); ImGui::DestroyContext();
        std::error_code error; std::filesystem::remove_all(root, error);
    }
    void Draw() {
        ImGui::NewFrame();
        ImGui::LogToBuffer();
        panel.Draw(root, "main.rice", scene, objectId);
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish(); ImGui::Render();
    }
};
}
int main() {
    try {
        Fixture fixture;
        fixture.Draw(); fixture.Draw();
        Check(ImGui::FindWindowByName("Streaming - YouTube Commands") && ImGui::FindWindowByName("Streaming - Cycle Timeline"), "Both real ImGui windows are built headlessly");
        Check(fixture.text.find("OnCycle") != std::string::npos && fixture.text.find("!add") != std::string::npos,
            "Reserved timeline row and registered command render in their windows");
        fixture.panel.StartPreview(fixture.scene);
        fixture.panel.QueuePreviewCommand("!add 3");
        fixture.panel.SubmitPreviewBatch();
        auto* object = fixture.scene.Find(fixture.objectId);
        Check(object->GetComponent<ControlledScript>()->total == 3 && fixture.actionCalls == 1,
            "Local test runs real receiver, native member and scheduler callback without an API client");
        const auto& log = fixture.panel.PreviewLog();
        auto response = std::find(log.begin(), log.end(), "OnResponse");
        auto cycle = std::find(log.begin(), log.end(), "OnCycle 1");
        Check(response != log.end() && cycle != log.end() && response < cycle, "Preview retains response-before-cycle order");
        fixture.panel.AdvancePreview(10); fixture.panel.SubmitPreviewBatch(false);
        Check(fixture.actionCalls == 2, "No-response local cycle still invokes permitted native action");
        fixture.Draw();
        CycleActionOverride override; override.timeFrom = 1.25f;
        fixture.panel.CycleModel().Patch("native-action", override);
        fixture.panel.SaveCycle();
        auto* listener = object->GetComponent<YouTubeListener>();
        listener->SetBindingConfiguration({{"add", "ControlledScript.Add", "!more {amount}", true, 0, "Everyone", {}}});
        fixture.panel.SaveCommands(fixture.scene);
        Check(std::filesystem::exists(fixture.root / "Streaming" / "cycle.json") &&
            std::filesystem::exists(fixture.root / "Streaming" / "commands.json"), "Project streaming settings persist as JSON");
        Scene replacement;
        auto& replacementObject = replacement.CreateGameObject("Audience controlled", fixture.objectId);
        replacementObject.AddComponent<ControlledScript>(); Register(replacementObject.AddComponent<YouTubeListener>());
        StreamingPanel restored; restored.commandsOpen = restored.cycleOpen = true;
        ImGui::NewFrame(); restored.Draw(fixture.root, "main.rice", replacement, fixture.objectId); ImGui::Render();
        Check(restored.CycleModel().EffectiveActions()[0].timeFrom == 1.25f &&
            replacementObject.GetComponent<YouTubeListener>()->BuildBindings()[0].pattern.Text() == "!more {amount}",
            "Project reload restores stable action overrides and selected-object native command binding");
        restored.StopPreview();
        std::cout << "Streaming panel tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "Streaming panel test failed: " << error.what() << '\n'; return 1; }
}
