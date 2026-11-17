#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "ControllerModel.h"
#include <cstdint>
#include <memory>
#include <future>

namespace RiceController {
bool SupportsControl(const std::filesystem::path& executable);
enum class RunState { Stopped, Starting, Running, Paused, Stopping, Error };
struct Status {
    RunState state = RunState::Stopped;
    bool alive = false, controlled = false, pending = false;
    bool canRetire = false, retiring = false;
    bool scriptReady = false;
    bool canRestoreSlot = false;
    DWORD pid = 0;
    std::string error, windowMode = "windowed", runId;
    uint64_t gameFrames = 0, renderFrames = 0;
};
class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    void Start(const Entry& entry, const std::string& mode, bool silentAudio = false,const std::string& mainSlot = {});
    void Pause(const std::string& id);
    void Resume(const std::string& id);
    void Stop(const std::string& id);
    // A delayed save acknowledgement must never stop a newly launched run.
    bool StopIfCurrent(const std::string& id, const std::string& expectedRunId);
    std::future<void> SaveForRetirement(const std::string& id, const std::string& expectedRunId);
    std::future<void> RestoreSlot(const std::string& id,const std::string& expectedRunId,const std::string& mainSlot);
    std::future<void> RestoreSlotForRun(const std::string& runtimeId,const std::string& mainSlot);
    void ForceStop(const std::string& id);
    void ContinueWaiting(const std::string& id);
    void SetWindowMode(const std::string& id, const std::string& mode);
    void Poll();
    Status Get(const std::string& id) const;
    bool AnyAlive() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
