#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Rice::Streaming {

struct CycleTicket {
    std::uint64_t generation = 0;
    std::uint64_t cycle = 0;
    friend bool operator==(const CycleTicket&, const CycleTicket&) = default;
};

struct CancellationState {
    std::atomic<bool> requested = false;
    std::atomic<bool> paused = false;
};

struct CycleContext {
    CycleTicket ticket;
    bool hasResponse = false;
    std::shared_ptr<const CancellationState> cancellation;
};

// All callbacks run on the scheduler's owner thread. Long work must cooperate
// with cancellation and return a pollable task instead of blocking that thread.
struct ActionTask {
    std::function<bool()> poll; // true = complete; absent = synchronous completion
    std::function<void()> cancel;
    std::function<void(bool)> pause;
};

struct CycleAction {
    enum class from { prev, cycleStart, response };
    std::string ActionId;
    std::function<ActionTask(const CycleContext&)> invoke;
    from reference = from::cycleStart;
    float timeFrom = 0.0f;
    bool isCycleBlocked = false;
    bool isStoppedByCycle = false;
    bool isActionOnNoResponse = true;
    CycleAction& From(from value = from::cycleStart) noexcept {
        reference = value;
        return *this;
    }
};
using CycleAciton = CycleAction; // compatibility with the original spelling

// Value snapshot. Row zero is immutable OnCycle; edits use Set/Add/Move so
// callers cannot accidentally mutate its identity or scheduling flags.
class CycleActionList {
public:
    CycleActionList();
    double cycleTime = 25.0;
    std::size_t Count() const noexcept;
    const CycleAction& operator[](std::size_t index) const;
    void Add(CycleAction action);
    void Insert(std::size_t index, CycleAction action);
    void Set(std::size_t index, CycleAction action);
    bool Remove(const CycleAction& action);
    void RemoveAt(std::size_t index);
    void Clear();
    void Move(std::size_t from, std::size_t to);
    bool Contains(const CycleAction& action) const noexcept;
    std::ptrdiff_t IndexOf(const CycleAction& action) const noexcept;
    std::ptrdiff_t Indexof(const CycleAction& action) const noexcept { return IndexOf(action); }
    auto begin() const noexcept { return actions_.begin(); }
    auto end() const noexcept { return actions_.end(); }
    void Validate() const;
private:
    std::vector<CycleAction> actions_;
};

struct CycleBatch {
    CycleTicket ticket;
    bool hasResponse = false;
    std::vector<std::function<void(const CycleContext&)>> commands;
};

class CycleScheduler {
public:
    using Clock = std::function<double()>; // monotonic seconds
    enum class State { Stopped, WaitingReady, Collecting, Executing };

    explicit CycleScheduler(Clock clock = {});
    ~CycleScheduler();
    CycleScheduler(const CycleScheduler&) = delete;
    CycleScheduler& operator=(const CycleScheduler&) = delete;

    std::function<void(CycleTicket)> onBatchRequested;
    std::function<void(const CycleContext&)> onCycle;
    std::function<void(CycleTicket)> onResponse;
    std::function<void(const std::string&)> onDiagnostic;

    void Start(std::uint64_t generation, bool gameReady, bool chatReady);
    void SetReady(bool gameReady, bool chatReady);
    void Stop();
    void Pause();
    void Resume();
    void Tick();
    // Submit only on the owner thread; a network worker must marshal here.
    bool NotifyResponse(std::uint64_t generation);
    bool SubmitBatch(CycleBatch batch);
    void SetDefinition(CycleActionList definition, std::uint64_t revision);

    State GetState() const noexcept { return state_; }
    bool IsPaused() const noexcept { return paused_; }
    CycleTicket CurrentTicket() const noexcept { return ticket_; }
    std::uint64_t AppliedRevision() const noexcept { return appliedRevision_; }
    double RemainingSeconds();
    std::size_t ActiveTaskCount() const noexcept { return active_.size(); }
private:
    struct EntryState { bool started = false; std::optional<double> completed; };
    struct Running {
        std::string id;
        CycleTicket ticket;
        std::size_t index = 0;
        ActionTask task;
        std::shared_ptr<CancellationState> cancellation;
        bool blocked = false;
        bool stoppedByCycle = false;
        bool cancelSent = false;
    };
    struct Response { CycleTicket ticket; double logicalTime; };

    Clock clock_;
    double lastWall_ = 0.0;
    double logicalTime_ = 0.0;
    double cycleStart_ = 0.0;
    double responseAnchor_ = 0.0;
    bool hasResponse_ = false;
    bool paused_ = false;
    bool dispatchBlocking_ = false;
    bool inTick_ = false;
    bool gameReady_ = false;
    bool chatReady_ = false;
    State state_ = State::Stopped;
    CycleTicket ticket_;
    std::optional<std::uint64_t> lastGeneration_;
    CycleActionList definition_;
    CycleActionList pending_;
    std::uint64_t appliedRevision_ = 0;
    std::uint64_t pendingRevision_ = 0;
    std::vector<EntryState> entries_;
    std::vector<std::shared_ptr<Running>> active_;
    std::vector<Response> responses_;
    std::optional<double> latestResponse_;
    std::shared_ptr<CancellationState> batchCancellation_;

    void AccountTime();
    bool IsBlocked() const;
    void RequestBatch();
    void DispatchResponses();
    void PollTasks();
    void StartReadyActions();
    void EndCycle();
    void Cancel(Running& running);
    void Diagnostic(const std::string& message) noexcept;
};

} // namespace Rice::Streaming

namespace Rice {
using CycleAction = Streaming::CycleAction;
using CycleAciton = CycleAction;
template<class T> class List;
template<> class List<CycleAction> : public Streaming::CycleActionList {};
using CycleTimeline = List<CycleAction>;
} // namespace Rice
