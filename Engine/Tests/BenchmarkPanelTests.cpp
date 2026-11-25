#include "Editor/BenchmarkPanel.h"
#include <imgui_internal.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Rice;
using namespace std::chrono_literals;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void SampleWork() {
    {
        BenchmarkScope large("Slow update", "Game/Update.cpp", 1);
        std::this_thread::sleep_for(8ms);
    }
    // Small adjacent shares should initially be presented as one misc. band.
    constexpr const char* scripts[] = {"Game/Script01.cpp", "Game/Script02.cpp", "Game/Script03.cpp",
        "Game/Script04.cpp", "Game/Script05.cpp", "Game/Script06.cpp", "Game/Script07.cpp",
        "Game/Script08.cpp", "Game/Script09.cpp", "Game/Script10.cpp", "Game/Script11.cpp", "Game/Script12.cpp"};
    for (const char* script : scripts) {
        BenchmarkScope small("Small update", script, 2);
        auto end = std::chrono::steady_clock::now() + 20us;
        while (std::chrono::steady_clock::now() < end) {}
    }
}

struct PanelFixture {
    BenchmarkPanel panel;
    bool open = true;
    std::string text;

    PanelFixture() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900, 600};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~PanelFixture() { ImGui::DestroyContext(); }

    uint64_t Draw(ImVec2 mouse = {-1, -1}, bool down = false, float wheel = 0, bool sample = false) {
        BenchmarkFrame frame;
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        if (wheel) io.AddMouseWheelEvent(0, wheel);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::LogToBuffer();
        panel.Draw(open);
        // Starting a diagnosis cannot retroactively capture work performed
        // earlier in the frame while collection was disabled.
        if (sample) SampleWork();
        text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::Render();
        return Benchmark::CurrentFrame();
    }

    // Find controls through ImGui hit-testing, independent of their layout.
    ImVec2 FindButton(const char* label, int threadIndex = -1) {
        auto* window = ImGui::FindWindowByName("Benchmark");
        auto seed = window->IDStack.back();
        if (threadIndex >= 0) seed = ImHashData(&threadIndex, sizeof(threadIndex), seed);
        auto id = ImHashStr(label, 0, seed);
        for (float y = window->Pos.y + 15; y < window->Pos.y + window->Size.y; y += 8) {
            for (float x = window->Pos.x + 8; x < window->Pos.x + window->Size.x; x += 8) {
                Draw({x, y});
                if (ImGui::GetCurrentContext()->HoveredId == id) return {x, y};
            }
        }
        throw std::runtime_error(std::string("Cannot find control: ") + label);
    }

    bool ThreadExpanded(uint64_t id) const {
        auto* window = ImGui::FindWindowByName("Benchmark");
        auto label = "###BenchmarkThread" + std::to_string(id);
        return window->StateStorage.GetInt(window->GetID(label.c_str()), 0) != 0;
    }

    ImVec2 MiscCenter() const {
        // Look for the actual grey fill emitted by the band renderer.
        const auto& vertices = ImGui::FindWindowByName("Benchmark")->DrawList->VtxBuffer;
        for (int i = 0; i + 3 < vertices.Size; ++i) {
            auto& first = vertices[i];
            if (first.col != IM_COL32(92, 96, 105, 255) || vertices[i + 2].col != first.col) continue;
            auto last = vertices[i + 2].pos;
            if (last.x - first.pos.x >= 2 && last.y > first.pos.y)
                return {(first.pos.x + last.x) / 2, (first.pos.y + last.y) / 2};
        }
        throw std::runtime_error("No clickable misc. band rendered");
    }
};

void WaitForCapture() {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!Benchmark::Result() || Benchmark::GetStatus().active || Benchmark::GetStatus().finalizing) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    Check(bool(Benchmark::Result()) && !Benchmark::GetStatus().active && !Benchmark::GetStatus().finalizing, "Capture did not finish");
}

void TestPanel(const std::filesystem::path& directory) {
    BenchmarkSession session(directory / "panel.tsv");
    Benchmark::BeginPlay();
    PanelFixture ui;
    ui.Draw();
    auto play = ui.FindButton("Play");
    ui.Draw(play, true);
    auto clickedFrame = ui.Draw(play, false, 0, true);
    WaitForCapture();
    auto result = Benchmark::Result();
    Check(result && result->frames.front().number == clickedFrame && result->frames.front().calls.size() == 13,
          "Play did not capture work following the click on the current frame");

    ui.Draw();
    auto rate = ui.FindButton("Rate");
    ui.Draw(rate, true);
    ui.Draw(rate);
    Check(ui.text.find("Share of measured self time") != std::string::npos, "Rate control did not switch mode");
    auto misc = ui.MiscCenter();
    ui.Draw(misc, true);
    ui.Draw(misc);
    Check(ui.text.find("Calls 1 - ") != std::string::npos && ui.text.find("Wheel to scroll") != std::string::npos,
          "Clicking misc. did not open the scrollable call list");
    ui.Draw(misc, false, -1);
    Check(ui.text.find("Calls 3 - ") != std::string::npos, "Wheel did not scroll the selected calls");
    ui.Draw();
    Check(ui.text.find("Calls ") == std::string::npos, "Call list stayed visible after leaving its band");

    ui.Draw(misc, false, 40);
    ui.Draw(misc, true);
    ui.Draw(misc);
    Check(ui.text.find("Calls 1 - 1 of 1") != std::string::npos && ui.text.find("Small update") != std::string::npos,
          "Zoom did not resolve the misc. group into individual scripts");
}

void TestEditControls(const std::filesystem::path& directory) {
    BenchmarkSession session(directory / "edit-controls.tsv");
    PanelFixture ui;
    for (int i = 0; i < 3; ++i) Check(ui.Draw() == 0, "Edit mode advanced the scene frame");
    Check(ui.text.find("Frame #0") != std::string::npos, "Edit mode did not display frame zero");
    auto schedule = ui.FindButton("Schedule");
    ui.Draw(schedule, true);
    ui.Draw(schedule);
    Check(Benchmark::GetStatus().active && Benchmark::GetStatus().sampled == 0,
          "Cannot schedule the first scene frame before Play");
    Benchmark::BeginPlay();
    Check(ui.Draw({-1, -1}, false, 0, true) == 1, "Scene Play did not start at frame one");
    WaitForCapture();
    auto result = Benchmark::Result();
    Check(result->frames.size() == 1 && result->frames.front().number == 1,
          "Schedule from edit mode sampled the wrong frame");
    auto play = ui.FindButton("Play");
    Benchmark::EndPlay();
    ui.Draw(play, true);
    ui.Draw(play);
    Check(Benchmark::CurrentFrame() == 0 && !Benchmark::GetStatus().active,
          "Benchmark Play was enabled while the scene was stopped");
}

void TestThreadBands() {
    BenchmarkResult result{{
        {1, 100, {{"GameRoot", "Game/Main.cpp", 1, 0, 0, 100, 60, 0},
                  {"WorkerRoot", "Game/Jobs.cpp", 2, 0, 10, 60, 40, 7},
                  {"GameChild", "Game/Main.cpp", 3, 1, 20, 40, 40, 0},
                  {"WorkerChild", "Game/Jobs.cpp", 4, 1, 30, 20, 20, 7}}}
    }, {{0, "Game"}, {7, "Jobs"}}};
    BenchmarkBands time(result, BenchmarkView::Time, BenchmarkGroup::Function);
    Check(time.threads.size() == 2 && time.rows.size() == 4 && time.extent == 100,
          "Threads did not receive separate depth lanes on the same time axis");
    Check(time.threads[0].firstRow == 0 && time.threads[0].rowCount == 2 &&
          time.threads[1].firstRow == 2 && time.threads[1].rowCount == 2,
          "Thread row boundaries overlap");
    Check(time.compactRows[0].size() == 1 && time.compactRows[0][0].logs.size() == 2 &&
          time.compactRows[1][0].logs.size() == 2 &&
          time.compactRows[1][0].logs[1].call->threadId == 7,
          "Collapsed time bands lost nested or worker logs");
    BenchmarkBands scripts(result, BenchmarkView::Time, BenchmarkGroup::Script);
    Check(scripts.rows.size() == 2 && scripts.rows[0].front().begin == 0 &&
          scripts.rows[0].back().end == 100 && scripts.rows[1].front().begin == 10 &&
          scripts.rows[1].back().end == 70, "Interleaved threads corrupted script time slices");
    BenchmarkBands rate(result, BenchmarkView::Rate, BenchmarkGroup::Function);
    Check(rate.extent == 160 && rate.rows[0].back().end == 100 && rate.rows[2].back().end == 60,
          "Rate must use each thread's exclusive time and the combined CPU-time denominator");
    Check(rate.compactRows[0][0].logs.size() == 2 && rate.compactRows[1][0].logs.size() == 2,
          "Collapsed Rate bands lost logs");
}

void TestThreadControls(const std::filesystem::path& directory) {
    BenchmarkSession session(directory / "thread-controls.tsv");
    Benchmark::BeginPlay();
    {
        BenchmarkFrame frame;
        Check(Benchmark::Schedule(1, 1, 1), "Cannot schedule thread fixture");
        BenchmarkScope root("Game parent", "Game/Main.cpp", 1);
        {
            BenchmarkScope child("Game child", "Game/Main.cpp", 2);
            BenchmarkScope leaf("Game leaf", "Game/Main.cpp", 3);
            std::this_thread::sleep_for(1ms);
        }
        std::thread worker([] {
            Benchmark::SetThreadName("Jobs");
            BenchmarkScope parent("Job parent", "Game/Jobs.cpp", 1);
            BenchmarkScope child("Job child", "Game/Jobs.cpp", 2);
            std::this_thread::sleep_for(1ms);
        });
        worker.join();
    }
    WaitForCapture();
    auto result = Benchmark::Result();
    auto worker = std::find_if(result->threads.begin(), result->threads.end(), [](const auto& thread) {
        return thread.name == "Jobs";
    });
    Check(worker != result->threads.end(), "Worker thread metadata is missing");
    PanelFixture ui;
    ui.Draw();
    ui.Draw();
    Check(ui.text.find("Game | Thread #0") != std::string::npos && ui.text.find("Jobs | Thread #") != std::string::npos,
          "The panel did not display every captured thread");
    Check(ui.ThreadExpanded(0) && ui.ThreadExpanded(worker->id), "Thread lanes must initially be expanded");
    auto gameHeader = ui.FindButton("###BenchmarkThread0");
    ui.Draw(gameHeader, true);
    ui.Draw(gameHeader);
    Check(!ui.ThreadExpanded(0) && ui.ThreadExpanded(worker->id), "Collapsing Game also changed Jobs");

    auto compact = ui.FindButton("##BenchmarkBands", 0);
    compact.x = ImGui::FindWindowByName("Benchmark")->Size.x * .5f;
    compact.y += ImGui::GetTextLineHeight() + 15;
    ui.Draw(compact, true);
    ui.Draw(compact);
    Check(ui.text.find("Calls 1 - 3 of 3") != std::string::npos &&
          ui.text.find("Game child") != std::string::npos && ui.text.find("Game leaf") != std::string::npos,
          "A collapsed thread band did not retain all nested call logs");

    auto workerLabel = "###BenchmarkThread" + std::to_string(worker->id);
    auto workerHeader = ui.FindButton(workerLabel.c_str());
    ui.Draw(workerHeader, true);
    ui.Draw(workerHeader);
    Check(!ui.ThreadExpanded(0) && !ui.ThreadExpanded(worker->id), "Worker collapse state was not independent");
    gameHeader = ui.FindButton("###BenchmarkThread0");
    ui.Draw(gameHeader, true);
    ui.Draw(gameHeader);
    Check(ui.ThreadExpanded(0) && !ui.ThreadExpanded(worker->id), "Expanding Game also expanded Jobs");
    auto rate = ui.FindButton("Rate");
    ui.Draw(rate, true);
    ui.Draw(rate);
    Check(ui.ThreadExpanded(0) && !ui.ThreadExpanded(worker->id) && ui.text.find("Jobs | Thread #") != std::string::npos,
          "Rate mode lost a thread or its collapse state");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "Expected a test output directory");
        TestThreadBands();
        TestEditControls(argv[1]);
        TestPanel(argv[1]);
        TestThreadControls(argv[1]);
        std::cout << "Benchmark panel interactions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
