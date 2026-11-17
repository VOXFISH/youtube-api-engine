#pragma once
#include "../ControllerModel.h"
#include "../ProcessManager.h"
#include <memory>

namespace RiceController::Streaming {
class StreamingPanel {
public:
    explicit StreamingPanel(const std::filesystem::path& database);
    ~StreamingPanel();
    StreamingPanel(const StreamingPanel&)=delete;
    StreamingPanel& operator=(const StreamingPanel&)=delete;
    void Tick(const std::vector<Entry>& entries,const ProcessManager& processes);
    void Draw(const std::vector<Entry>& entries,ProcessManager& processes,bool stopping);
    void DrawWindows(const std::vector<Entry>& entries,const ProcessManager& processes);
    bool Busy() const;
    bool Ready() const;
    void OpenManagement(const std::string& slotId);
    bool HasSlotForEntry(const std::string& entryId) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
