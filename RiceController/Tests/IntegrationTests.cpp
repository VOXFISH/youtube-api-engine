#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "../Source/ProcessManager.h"
#include "Streaming/GameReplacementFlow.h"
#include "Streaming/PlayerOutput.h"
#include "../Shared/PlayerMediaChannel.h"
#include "Engine/Data/SqliteStore.h"
#include "Engine/Data/DataValueCodec.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace RiceController;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
std::vector<std::string> checks;
std::vector<DWORD> recordedPids;

void Check(bool value, const std::string& description) {
    if (!value) throw std::runtime_error(description);
    checks.push_back(description);
}

struct ProcessHandle {
    HANDLE value = nullptr;
    explicit ProcessHandle(DWORD pid) : value(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid)) {
        if (!value) throw std::runtime_error("Cannot retain test process handle");
    }
    ~ProcessHandle() { if (value) CloseHandle(value); }
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;
    ProcessHandle(ProcessHandle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    DWORD ExitCode() const {
        DWORD code = STILL_ACTIVE;
        if (!GetExitCodeProcess(value, &code)) throw std::runtime_error("Cannot query test exit code");
        return code;
    }
};

template<class Predicate>
void Wait(ProcessManager& manager, Predicate ready, const std::string& description, std::chrono::seconds timeout = 35s) {
    auto deadline = Clock::now() + timeout;
    do {
        manager.Poll();
        if (ready()) return;
        std::this_thread::sleep_for(10ms);
    } while (Clock::now() < deadline);
    throw std::runtime_error("Timed out: " + description);
}

void Pump(ProcessManager& manager, std::chrono::milliseconds duration) {
    auto deadline = Clock::now() + duration;
    do {
        manager.Poll();
        std::this_thread::sleep_for(10ms);
    } while (Clock::now() < deadline);
}

void Ready(ProcessManager& manager, const Entry& entry, bool controlled) {
    Wait(manager, [&] {
        auto status = manager.Get(entry.id);
        if (status.state == RunState::Error) throw std::runtime_error("Player startup: " + status.error);
        return status.alive && status.state == RunState::Running && status.controlled == controlled && !status.pending;
    }, "player ready: " + entry.name);
}

HWND FindPlayerWindow(DWORD pid) {
    struct Search { DWORD pid; HWND window = nullptr; } search{pid};
    EnumWindows([](HWND window, LPARAM opaque) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(opaque);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        wchar_t className[64]{};
        GetClassNameW(window, className, 64);
        if (owner == search.pid && std::wstring(className) == L"GLFW30") {
            search.window = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

std::map<fs::path, std::string> ProjectFiles(const fs::path& project) {
    std::map<fs::path, std::string> result;
    for (const auto& file : fs::recursive_directory_iterator(project)) {
        if (!file.is_regular_file()) continue;
        std::ifstream input(file.path(), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read project fingerprint input");
        result.emplace(fs::relative(file.path(), project), std::string(std::istreambuf_iterator<char>(input), {}));
    }
    return result;
}

void CopyRuntime(const fs::path& source, const fs::path& destination, bool marker) {
    fs::create_directories(destination);
    fs::copy_file(source, destination / L"RicePlayer.exe");
    for (const auto& file : fs::directory_iterator(source.parent_path())) {
        if (file.is_regular_file() && file.path().extension() == L".dll")
            fs::copy_file(file.path(), destination / file.path().filename());
    }
    if (marker) fs::copy_file(source.parent_path() / L"RicePlayer.control", destination / L"RicePlayer.control");
}

Entry MakeEntry(const fs::path& executable, const fs::path& project, const std::string& name) {
    Entry entry;
    entry.id = NewId();
    entry.name = name;
    entry.executable = executable;
    entry.project = project;
    entry.windowMode = "hidden";
    ValidateEntry(entry);
    return entry;
}

void StopAndCheck(ProcessManager& manager, const Entry& entry, const ProcessHandle& process) {
    Wait(manager, [&] { return !manager.Get(entry.id).pending; }, "previous command completed", 5s);
    if(manager.Get(entry.id).canRetire){
        auto saved=manager.SaveForRetirement(entry.id,manager.Get(entry.id).runId);
        Wait(manager,[&]{return saved.wait_for(0s)==std::future_status::ready;},"Player durable retirement acknowledgement");
        saved.get();
        const auto before=manager.Get(entry.id);
        Pump(manager,600ms);
        const auto after=manager.Get(entry.id);
        Check(after.alive && after.gameFrames==before.gameFrames && after.renderFrames==before.renderFrames,
            "Player stays alive and quiesced after durable save acknowledgement until Stop");
    }
    Check(!manager.StopIfCurrent(entry.id,"stale-runtime-id"), "Stale save completion cannot stop a live player");
    Check(manager.StopIfCurrent(entry.id,manager.Get(entry.id).runId), "Matching runtime permits graceful stop");
    Wait(manager, [&] { return !manager.Get(entry.id).alive; }, "normal player shutdown", 10s);
    Check(manager.Get(entry.id).state == RunState::Stopped, "Normal close becomes Stopped");
    Check(process.ExitCode() == 0, "Normal close has exit code zero");
}

std::string Quoted(const std::string& text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c >= 32) result += static_cast<char>(c);
    }
    return result + '"';
}

void WriteReport(const fs::path& output, bool passed, const std::string& failure) {
    std::ofstream report(output / L"results.json", std::ios::binary);
    report << "{\n  \"passed\": " << (passed ? "true" : "false") << ",\n  \"failure\": " << Quoted(failure)
           << ",\n  \"checks\": [";
    for (size_t i = 0; i < checks.size(); ++i) report << (i ? ",\n    " : "\n    ") << Quoted(checks[i]);
    report << "\n  ],\n  \"pids\": [";
    for (size_t i = 0; i < recordedPids.size(); ++i) report << (i ? ", " : "") << recordedPids[i];
    report << "]\n}\n";
}

void Exercise(const fs::path& executable, const fs::path& project, const fs::path& output,bool withMedia) {
    auto runtime = output / L"Runtime with spaces";
    auto legacyRuntime = output / L"Legacy runtime";
    auto copiedProject = output / L"프로젝트 공유 폴더";
    CopyRuntime(executable, runtime, true);
    CopyRuntime(executable, legacyRuntime, false);
    fs::copy(project, copiedProject, fs::copy_options::recursive);
    auto before = ProjectFiles(copiedProject);
    const auto player = runtime / L"RicePlayer.exe";
    Check(SupportsControl(player), "Control capability marker recognized");
    Check(!SupportsControl(legacyRuntime / L"RicePlayer.exe"), "Missing marker selects legacy mode");

    ProcessManager manager;
    std::array<Entry, 3> entries{
        MakeEntry(player, copiedProject, "빌드 1"),
        MakeEntry(player, copiedProject, "빌드 2"),
        MakeEntry(player, copiedProject, "빌드 3")
    };
    for (const auto& entry : entries) manager.Start(entry, "hidden", true);
    for (const auto& entry : entries) Ready(manager, entry, true);
    std::set<DWORD> distinct;
    std::vector<ProcessHandle> handles;
    for (const auto& entry : entries) {
        auto status = manager.Get(entry.id);
        distinct.insert(status.pid);
        recordedPids.push_back(status.pid);
        handles.emplace_back(status.pid);
        auto window = FindPlayerWindow(status.pid);
        Check(window && !IsWindowVisible(window), "Hidden player has a hidden native window");
    }
    Check(distinct.size() == 3, "Three simultaneous entries have distinct process IDs");
    const auto firstPid = manager.Get(entries[0].id).pid;
    manager.Start(entries[0], "windowed", true);
    Check(manager.Get(entries[0].id).pid == firstPid && manager.Get(entries[0].id).windowMode == "hidden",
          "Repeated Start preserves the existing process and mode");
    Wait(manager, [&] {
        for (const auto& entry : entries) {
            auto status = manager.Get(entry.id);
            if (status.gameFrames < 5 || status.pending) return false;
        }
        return true;
    }, "all hidden games progressing");
    Check(true, "All three hidden games advance frames");

    manager.Pause(entries[0].id);
    Wait(manager, [&] { auto s = manager.Get(entries[0].id); return s.state == RunState::Paused && !s.pending; }, "pause acknowledgement");
    const auto paused = manager.Get(entries[0].id);
    const auto peerFrames = manager.Get(entries[1].id).gameFrames;
    Pump(manager, 1400ms);
    auto stillPaused = manager.Get(entries[0].id);
    Check(stillPaused.alive && stillPaused.pid == firstPid && stillPaused.gameFrames == paused.gameFrames,
          "Pause freezes game frames without replacing the process");
    Check(stillPaused.renderFrames > paused.renderFrames, "Paused player continues rendering and IPC");
    Check(manager.Get(entries[1].id).gameFrames > peerFrames, "Pausing one player leaves another game advancing");
    Wait(manager, [&] { return !manager.Get(entries[0].id).pending; }, "status query complete", 5s);
    manager.Resume(entries[0].id);
    Wait(manager, [&] { auto s = manager.Get(entries[0].id); return s.state == RunState::Running && s.gameFrames > paused.gameFrames && !s.pending; }, "resume gameplay");
    Check(manager.Get(entries[0].id).pid == firstPid, "Resume continues in the same process");

    manager.SetWindowMode(entries[0].id, "fullscreen");
    Wait(manager, [&] { auto s = manager.Get(entries[0].id); return s.windowMode == "fullscreen" && !s.pending; }, "fullscreen acknowledgement");
    auto window = FindPlayerWindow(firstPid);
    RECT bounds{};
    MONITORINFO monitor{sizeof(monitor)};
    Check(window && IsWindowVisible(window), "Fullscreen makes the same player window visible");
    Check(GetWindowRect(window, &bounds) && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor), "Fullscreen native geometry is available");
    Check(bounds.left == monitor.rcMonitor.left && bounds.top == monitor.rcMonitor.top &&
          bounds.right == monitor.rcMonitor.right && bounds.bottom == monitor.rcMonitor.bottom,
          "Fullscreen window exactly covers its monitor");
    Check((GetWindowLongPtrW(window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) == 0,
          "Fullscreen is borderless");
    Check(manager.Get(entries[0].id).pid == firstPid, "Fullscreen preserves PID");
    const auto beforeHide = manager.Get(entries[0].id).gameFrames;
    manager.SetWindowMode(entries[0].id, "hidden");
    Wait(manager, [&] { auto s = manager.Get(entries[0].id); return s.windowMode == "hidden" && s.gameFrames > beforeHide && !s.pending; }, "hidden mode continues gameplay");
    Check(!IsWindowVisible(window) && manager.Get(entries[0].id).pid == firstPid, "Hiding fullscreen keeps the process and removes its window from display");

    StopAndCheck(manager, entries[1], handles[1]);
    const auto firstFrames = manager.Get(entries[0].id).gameFrames;
    const auto thirdFrames = manager.Get(entries[2].id).gameFrames;
    Wait(manager, [&] { return manager.Get(entries[0].id).gameFrames > firstFrames && manager.Get(entries[2].id).gameFrames > thirdFrames; }, "survivors advance");
    Check(manager.Get(entries[0].id).alive && manager.Get(entries[2].id).alive, "Stopping the middle player leaves both peers running");
    StopAndCheck(manager, entries[0], handles[0]);
    Wait(manager, [&] { return !manager.Get(entries[2].id).pending; }, "third player status query complete", 5s);
    manager.Pause(entries[2].id);
    Wait(manager, [&] { return manager.Get(entries[2].id).state == RunState::Paused && !manager.Get(entries[2].id).pending; }, "third player pause");
    StopAndCheck(manager, entries[2], handles[2]);
    Check(true, "A paused player still accepts normal Stop");
    Check(!manager.AnyAlive(), "Stopping all entries leaves no owned processes running");

    manager.Start(entries[0], "hidden", true);
    Ready(manager, entries[0], true);
    const auto restarted = manager.Get(entries[0].id);
    Check(!restarted.scriptReady,"Rendering alone does not imply script readiness");
    ProcessHandle restartHandle(restarted.pid);
    recordedPids.push_back(restarted.pid);
    Check(restarted.pid != firstPid, "Play after Stop starts a new process");
    StopAndCheck(manager, entries[0], restartHandle);

    // Add the explicit example script only to a new test project, never to the
    // user's project or the no-signal project used above.
    const auto readyProject=output/L"Script ready project";
    fs::copy(copiedProject,readyProject,fs::copy_options::recursive);
    const auto sceneFile=readyProject/L"main.rice";
    {
        std::ifstream input(sceneFile,std::ios::binary);
        std::string scene((std::istreambuf_iterator<char>(input)),{});
        const auto position=scene.find("EndObject");
        if(position==std::string::npos)throw std::runtime_error("Test scene has no object");
        scene.insert(position,"Component \"SignalStreamReady\"\n\"Enabled\" 1\nEndComponent\n");
        const auto dataPath=PathText(output/L"player-data.sqlite3");
        std::ostringstream component;
        component<<"Component \"StreamDataCounter\"\n\"Enabled\" 1\n\"Database path\" "<<std::quoted(dataPath)<<"\nEndComponent\n";
        scene.insert(position,component.str());
        std::ofstream file(sceneFile,std::ios::binary|std::ios::trunc);file<<scene;
    }
    auto readyEntry=MakeEntry(player,readyProject,"Script readiness");
    manager.Start(readyEntry,"hidden",true);Ready(manager,readyEntry,true);
    Wait(manager,[&]{return manager.Get(readyEntry.id).scriptReady;},"Script readiness signal propagated over IPC");
    ProcessHandle readyHandle(manager.Get(readyEntry.id).pid);recordedPids.push_back(manager.Get(readyEntry.id).pid);
    StopAndCheck(manager,readyEntry,readyHandle);

    // A candidate that writes slot data but deliberately never signals ready.
    const auto waitingProject=output/L"Preparing data project";
    fs::copy(readyProject,waitingProject,fs::copy_options::recursive);
    {
        const auto path=waitingProject/L"main.rice";
        std::ifstream input(path,std::ios::binary);
        std::string scene((std::istreambuf_iterator<char>(input)),{});input.close();
        const std::string signal="Component \"SignalStreamReady\"\n\"Enabled\" 1\nEndComponent\n";
        const auto position=scene.find(signal);
        Check(position!=std::string::npos,"Readiness test component exists");
        scene.erase(position,signal.size());
        std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<scene;
    }
    // Real Player capture, restore/save IPC, NVENC/AAC and local FLV output.
    // Chat readiness is controlled; no live YouTube broadcast is opened.
    {
        using namespace RiceController::Streaming;
        manager.Start(entries[0],"hidden",true);Ready(manager,entries[0],true);
        const auto oldRun=manager.Get(entries[0].id);
        ProcessHandle oldHandle(oldRun.pid);recordedPids.push_back(oldRun.pid);
        const auto imagePath=output/L"waiting.bmp";
        std::vector<unsigned char> bmp(62);bmp[0]='B';bmp[1]='M';bmp[2]=62;bmp[10]=54;bmp[14]=40;
        bmp[18]=2;bmp[22]=1;bmp[26]=1;bmp[28]=24;bmp[34]=8;bmp[56]=255;bmp[58]=255;
        {std::ofstream file(imagePath,std::ios::binary);file.write(reinterpret_cast<const char*>(bmp.data()),bmp.size());}
        SlotRegistry slots;Slot slot;slot.slotId="replacement";slot.name="Replacement";
        slot.connection={"test-broadcast","test-chat","test-account","test-ingest"};
        const auto imageUtf8=imagePath.u8string();slot.waitingImagePath.assign(imageUtf8.begin(),imageUtf8.end());
        slots.Add(slot);slots.ConfigureInstance(slot.slotId,entries[0].id,"old-game");slots.BindEntry(entries[0].id,oldRun.runId);
        slots.MarkConnecting(slot.slotId);
        const auto movie=output/L"replacement.flv";
        std::unique_ptr<PlayerOutput> media;
        if(withMedia){
            media=std::make_unique<PlayerOutput>(slot.profile,oldRun.runId,movie);auto mediaStarted=media->Started();
            Wait(manager,[&]{return mediaStarted.wait_for(0s)==std::future_status::ready;},"Real hidden Player capture starts local encoded output");
            mediaStarted.get();
        }
        slots.MarkLive(slot.slotId);
        const auto completed=[] {std::promise<void> p;auto f=p.get_future();p.set_value();return f;};
        std::promise<void> switchAck;bool switchRequested=false;
        ReplacementServices services;
        bool commandsMuted=false;
        services.muteCommands=[&](const auto&,bool muted){commandsMuted=muted;};
        services.showWaiting=[&](const auto&,auto image,std::stop_token token){Check(image->width==1920,"Waiting image prepared before launching replacement");return media?media->ShowWaiting(std::move(image),token):completed();};
        services.restoreData=[&](const auto& slotId,const auto& runtime,std::stop_token token){
            if(token.stop_requested())throw std::runtime_error("Restore cancelled");
            return manager.RestoreSlotForRun(runtime,slotId);
        };
        services.connectChat=[&](const auto&,const auto&,std::stop_token){return completed();};
        services.switchSource=[&](const auto&,const auto& run,std::stop_token token){
            auto changed=media?media->SwitchSource(run,token):completed();
            if(run==oldRun.runId)return changed;
            switchRequested=true;
            return std::async(std::launch::async,[changed=std::move(changed),gate=switchAck.get_future()]()mutable{changed.get();gate.get();});
        };
        GameReplacementFlow flow(slots,manager,std::move(services));
        auto noSignal=MakeEntry(player,waitingProject,"No readiness signal");
        auto readCounter=[&]{
            Rice::Data::SqliteStore data(output/L"player-data.sqlite3");
            Rice::Data::RouteKey key;key.domain="local";key.build="stream-sample";key.lifetime=Rice::Data::Lifetime::Slot;key.slot="replacement";key.dataId="counter";
            const auto saved=data.Get(key);return saved?Rice::Data::DecodeValue<int>(saved->value):-1;
        };
        const auto framesBeforeReplacement=manager.Get(entries[0].id).gameFrames;
        flow.Begin(slot.slotId,noSignal,"unready-game");
        Wait(manager,[&]{flow.Poll();return manager.Get(noSignal.id).renderFrames>1;},"Replacement renders without readiness signal");
        for(int i=0;i<10;++i){Pump(manager,20ms);flow.Poll();}
        Check(flow.State()==GameReplacementFlow::Phase::Preparing && !switchRequested && commandsMuted && manager.Get(entries[0].id).alive,
            "No script signal keeps waiting image and original Player alive");
        Wait(manager,[&]{flow.Poll();return manager.Get(entries[0].id).gameFrames>framesBeforeReplacement;},
            "Original game keeps updating while only the replacement slot input is muted");
        flow.Cancel();
        Wait(manager,[&]{flow.Poll();return flow.State()==GameReplacementFlow::Phase::Failed && !manager.Get(noSignal.id).alive;},"Cancel restores old source and retires unused candidate");
        Check(manager.Get(entries[0].id).alive,"Cancellation preserves old Player");
        Check(!commandsMuted,"Cancel resumes slot input without pausing the game");
        Check(readCounter()==2,"Cancellation preserves preparation and teardown slot writes after candidate exit");
        flow.Begin(slot.slotId,noSignal,"unready-game");
        Wait(manager,[&]{flow.Poll();return manager.Get(noSignal.id).renderFrames>1 && readCounter()==3;},
            "Retry restores saved preparation data and continues incrementing it");
        Check(!switchRequested,"Retry still requires an explicit script readiness signal");
        flow.Cancel();
        Wait(manager,[&]{flow.Poll();return flow.State()==GameReplacementFlow::Phase::Failed && !manager.Get(noSignal.id).alive;},
            "Repeated cancellation saves and gracefully stops the new candidate");
        Check(readCounter()==4 && manager.Get(entries[0].id).alive && !commandsMuted,
            "Repeated cancellation preserves cumulative data and restores old game input");

        auto replacement=MakeEntry(player,readyProject,"Ready replacement");
        flow.Begin(slot.slotId,replacement,"new-game");
        Wait(manager,[&]{flow.Poll();return switchRequested;},"Explicit script readiness reaches media switch");
        Check(manager.Get(entries[0].id).alive && slots.Get(slot.slotId).runtimeInstanceId==oldRun.runId,
            "No assignment or retirement before actual source-switch acknowledgement");
        switchAck.set_value();
        Wait(manager,[&]{flow.Poll();return flow.State()==GameReplacementFlow::Phase::Complete && !manager.Get(entries[0].id).alive;},
            "Committed replacement saves and gracefully stops original Player");
        Check(oldHandle.ExitCode()==0 && slots.Get(slot.slotId).entryId==replacement.id,"Replacement commits new identity and old process exits normally");
        ProcessHandle replacementHandle(manager.Get(replacement.id).pid);recordedPids.push_back(manager.Get(replacement.id).pid);
        StopAndCheck(manager,replacement,replacementHandle);
        {
            Rice::Data::SqliteStore data(output/L"player-data.sqlite3");
            Rice::Data::RouteKey key;key.domain="local";key.build="stream-sample";key.lifetime=Rice::Data::Lifetime::Slot;key.slot="replacement";key.dataId="counter";
            const auto saved=data.Get(key);
            Check(saved && Rice::Data::DecodeValue<int>(saved->value)==6,"Successful replacement continues cancelled preparation data and durably saves teardown writes");
        }
        if(media){media->Stop();Check(media->Error().empty() && fs::file_size(movie)>4096,"Real old/waiting/new source video and game-mix AAC finalized to local FLV");}
    }

    auto legacy = MakeEntry(legacyRuntime / L"RicePlayer.exe", copiedProject, "Legacy player");
    manager.Start(legacy, "windowed", true);
    Ready(manager, legacy, false);
    const auto legacyStatus = manager.Get(legacy.id);
    recordedPids.push_back(legacyStatus.pid);
    ProcessHandle legacyHandle(legacyStatus.pid);
    bool rejected = false;
    try { manager.Pause(legacy.id); } catch (const std::exception&) { rejected = true; }
    Check(rejected && manager.Get(legacy.id).alive, "Legacy mode rejects unsupported Pause without killing the player");
    rejected = false;
    try { manager.SetWindowMode(legacy.id, "hidden"); } catch (const std::exception&) { rejected = true; }
    Check(rejected && manager.Get(legacy.id).windowMode == "windowed", "Legacy mode rejects unsupported window control");
    StopAndCheck(manager, legacy, legacyHandle);

    Entry missing = entries[0];
    missing.id = NewId();
    missing.executable = output / L"missing-player.exe";
    rejected = false;
    try { manager.Start(missing, "hidden", true); } catch (const std::exception&) { rejected = true; }
    Check(rejected && !manager.Get(missing.id).alive, "Invalid executable never starts a child");

    auto crash = entries[0];
    crash.id = NewId();
    manager.Start(crash, "hidden", true);
    Ready(manager, crash, true);
    auto crashPid = manager.Get(crash.id).pid;
    ProcessHandle crashHandle(crashPid);
    recordedPids.push_back(crashPid);
    Check(TerminateProcess(crashHandle.value, 73) != FALSE, "Test-only crash is confined to its created process");
    Wait(manager, [&] { return !manager.Get(crash.id).alive; }, "crashed player observed");
    Check(manager.Get(crash.id).state == RunState::Error, "Unexpected process exit is reported as an error");

    HANDLE jobChild = nullptr;
    {
        ProcessManager temporaryOwner;
        auto owned = entries[0];
        owned.id = NewId();
        temporaryOwner.Start(owned, "hidden", true);
        Ready(temporaryOwner, owned, true);
        auto childPid = temporaryOwner.Get(owned.id).pid;
        recordedPids.push_back(childPid);
        jobChild = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, childPid);
        Check(jobChild != nullptr, "Job cleanup test retains its own child handle");
    }
    const auto cleanupResult = WaitForSingleObject(jobChild, 5000);
    CloseHandle(jobChild);
    Check(cleanupResult == WAIT_OBJECT_0, "Closing the controller owner terminates its job child");
    Check(ProjectFiles(copiedProject) == before, "All playback and control operations preserve project file bytes");
}
}

int wmain(int argc, wchar_t** argv) {
    fs::path output;
    try {
        SetProcessDPIAware();
        if (argc != 4 && !(argc==5 && std::wstring(argv[4])==L"--media")) throw std::runtime_error("Usage: RiceControllerIntegrationTests <RicePlayer.exe> <project-folder> <output-root> [--media]");
        auto executable = fs::absolute(argv[1]);
        auto project = fs::absolute(argv[2]);
        output = fs::absolute(argv[3]) / ("run-" + NewId());
        fs::create_directories(output);
        Exercise(executable, project, output,argc==5);
        WriteReport(output, true, {});
        std::cout << "Controller integration passed: " << checks.size() << " checks\nResults: " << PathText(output / L"results.json") << '\n';
        return 0;
    } catch (const std::exception& error) {
        if (!output.empty()) WriteReport(output, false, error.what());
        std::cerr << "Controller integration failed: " << error.what() << '\n';
        if (!output.empty()) std::cerr << "Results: " << PathText(output / L"results.json") << '\n';
        return 1;
    }
}
