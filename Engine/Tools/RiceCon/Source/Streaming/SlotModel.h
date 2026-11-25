#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace RiceController::Streaming {
enum class VideoCodec { H264, HEVC, AV1 };
enum class LiveLatency { Normal, Low, UltraLow };
enum class OutputState { Idle, Connecting, Live, Reconnecting, Failed };
enum class StreamEventKind { Start, End, MainSlotChanged };

struct OutputProfile {
    int width = 1920, height = 1080, fps = 60;
    VideoCodec codec = VideoCodec::H264;
    int videoBitrateKbps = 17000;
    bool constantBitrate = true;
    int keyframeSeconds = 2;
    std::string encoder = "auto";
    int audioBitrateKbps = 128, audioSampleRate = 48000;
    LiveLatency latency = LiveLatency::Low;
    int streamDelaySeconds = 0;
    bool autoReconnect = true;
    int reconnectAttempts = 20, reconnectInitialSeconds = 2;
    bool keepBroadcastOnSignalLoss = true;
    bool operator==(const OutputProfile&) const = default;
};
struct PauseSettings {
    bool stopOutput = false;
    bool muteGameAudio = true;
    std::string overlayPngPath;
    int overlayOpacityPercent = 100;
    bool operator==(const PauseSettings&) const = default;
};
struct YouTubeConnection {
    std::string broadcastId, liveChatId;
    // References into the credential store; never API keys or ingest secrets.
    std::string accountRef, streamCredentialRef;
    std::string ingestServer="rtmps://a.rtmps.youtube.com:443/live2";
    bool operator==(const YouTubeConnection&) const = default;
};
struct Slot {
    std::string slotId, name;
    std::string entryId, instanceTypeId;
    std::string runtimeInstanceId; // Fresh process identity, never restored from disk.
    bool isMain = false;
    OutputProfile profile;
    PauseSettings pause;
    std::string waitingImagePath; // Used with silence while preparing a replacement game.
    YouTubeConnection connection;
    OutputState state = OutputState::Idle;
    std::string lastError;
};
struct StreamEvent {
    StreamEventKind kind;
    std::string runtimeInstanceId, mainSlotId;
};

void ValidateProfile(const OutputProfile& profile);
void ValidatePause(const PauseSettings& pause);
const char* CodecName(VideoCodec codec);
const char* LatencyName(LiveLatency latency);
const char* OutputStateName(OutputState state);
bool IsActive(OutputState state); // Live or reconnecting after a confirmed start.
bool IsBusy(OutputState state);   // Includes the initial connection attempt.

// Controller thread only. This class models confirmed backend state; it does not
// open a socket, encode frames, call YouTube or pretend to implement RTMPS.
class SlotRegistry {
public:
    static constexpr std::size_t MaxConcurrentOutputs = 5;
    using EventSink = std::function<void(const StreamEvent&)>;
    explicit SlotRegistry(EventSink sink = {});
    const std::vector<Slot>& Slots() const noexcept;
    const Slot& Get(std::string_view slotId) const;
    void Add(Slot slot);
    void Remove(std::string_view slotId);
    void Rename(std::string_view slotId, std::string name);
    void ConfigureInstance(std::string_view slotId, std::string entryId, std::string instanceTypeId);
    void BindEntry(std::string_view entryId, std::string runtimeInstanceId);
    void UnbindEntry(std::string_view entryId);
    void SetInstanceType(std::string_view entryId, std::string instanceTypeId);
    // Preserves this slot's confirmed output while changing its input instance.
    // A main slot must first hand off its role before leaving a multi-slot group.
    void Assign(std::string_view slotId, std::string entryId,
                std::string instanceTypeId, std::string runtimeInstanceId);
    void ValidateAssignment(std::string_view slotId, const std::string& entryId,
                            const std::string& instanceTypeId, const std::string& runtimeInstanceId) const;
    void ClearAssignment(std::string_view slotId);
    void SetMain(std::string_view slotId);
    void SetProfile(std::string_view slotId, OutputProfile profile);
    void SetPause(std::string_view slotId, PauseSettings settings);
    void SetWaitingImage(std::string_view slotId, std::string path);
    void SetConnection(std::string_view slotId, YouTubeConnection connection);
    void MarkConnecting(std::string_view slotId);
    void MarkLive(std::string_view slotId);
    void MarkReconnecting(std::string_view slotId, std::string error);
    void MarkStopped(std::string_view slotId, std::string error = {});
    void StopAll();
    std::size_t ActiveCount(std::string_view runtimeInstanceId) const;
    std::string MainSlotFor(std::string_view runtimeInstanceId) const;
    // Configuration only: no live state, runtime IDs, or credential values.
    std::string Serialize() const;
    static SlotRegistry Deserialize(std::string_view json, EventSink sink = {});
private:
    std::vector<Slot> slots_;
    EventSink sink_;
    Slot& Mutable(std::string_view slotId);
    void CheckOwnership(const Slot& candidate) const;
    void CheckMainDeparture(const Slot& slot) const;
    void Emit(StreamEventKind kind, const std::string& runtimeId,
              const std::string& mainSlotId) const noexcept;
};
}
