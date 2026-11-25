#pragma once
#include "EncodedOutput.h"
#include "WaitingImage.h"
#include <future>
#include <stop_token>

namespace RiceController::Streaming {
// One independent worker per destination. Source switches are serialized with
// encoding/mux writes; acknowledgements follow the first submitted video packet
// from the new source. Controller owns this object's lifetime.
class PlayerOutput {
public:
    PlayerOutput(OutputProfile profile,std::string runtimeId,std::filesystem::path localFile);
    PlayerOutput(OutputProfile profile,std::string runtimeId,std::string rtmpsUrl);
    ~PlayerOutput();
    PlayerOutput(const PlayerOutput&)=delete;
    std::future<void> Started();
    std::future<void> ShowWaiting(std::shared_ptr<const RgbaImage> image,std::stop_token cancelled);
    std::future<void> SwitchSource(std::string runtimeId,std::stop_token cancelled);
    void Stop();
    void RequestStop() noexcept;
    bool Finished() const;
    std::string Error() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
