#include "CycleScheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Rice::Streaming {
namespace {
constexpr const char* ReservedAction = "__rice_on_cycle__";
void ValidateAction(const CycleAction& action) {
    if (action.ActionId.empty() || action.ActionId == ReservedAction)
        throw std::invalid_argument("CycleAction requires a non-reserved stable ActionId");
    if (!std::isfinite(action.timeFrom) || action.timeFrom < 0.0f)
        throw std::invalid_argument("CycleAction timeFrom must be finite and non-negative");
    if (!action.invoke) throw std::invalid_argument("CycleAction requires an invocation");
    if (action.reference != CycleAction::from::prev &&
        action.reference != CycleAction::from::cycleStart &&
        action.reference != CycleAction::from::response)
        throw std::invalid_argument("Unknown CycleAction time reference");
}
void Editable(std::size_t index, std::size_t count) {
    if (index == 0) throw std::invalid_argument("OnCycle row zero is immutable");
    if (index >= count) throw std::out_of_range("CycleAction index");
}
}

CycleActionList::CycleActionList() {
    CycleAction reserved;
    reserved.ActionId = ReservedAction;
    actions_.push_back(std::move(reserved));
}
std::size_t CycleActionList::Count() const noexcept { return actions_.size(); }
const CycleAction& CycleActionList::operator[](std::size_t index) const { return actions_.at(index); }
void CycleActionList::Add(CycleAction action) { Insert(Count(), std::move(action)); }
void CycleActionList::Insert(std::size_t index, CycleAction action) {
    if (index == 0) throw std::invalid_argument("Cannot insert before OnCycle");
    if (index > Count()) throw std::out_of_range("CycleAction insert index");
    ValidateAction(action);
    if (Contains(action)) throw std::invalid_argument("Duplicate ActionId");
    actions_.insert(actions_.begin() + static_cast<std::ptrdiff_t>(index), std::move(action));
}
void CycleActionList::Set(std::size_t index, CycleAction action) {
    Editable(index, Count());
    ValidateAction(action);
    auto existing = IndexOf(action);
    if (existing >= 0 && existing != static_cast<std::ptrdiff_t>(index))
        throw std::invalid_argument("Duplicate ActionId");
    actions_[index] = std::move(action);
}
bool CycleActionList::Remove(const CycleAction& action) {
    auto index = IndexOf(action);
    if (index < 0) return false;
    RemoveAt(static_cast<std::size_t>(index));
    return true;
}
void CycleActionList::RemoveAt(std::size_t index) {
    Editable(index, Count());
    actions_.erase(actions_.begin() + static_cast<std::ptrdiff_t>(index));
}
void CycleActionList::Clear() { actions_.erase(actions_.begin() + 1, actions_.end()); }
void CycleActionList::Move(std::size_t from, std::size_t to) {
    Editable(from, Count());
    Editable(to, Count());
    auto action = std::move(actions_[from]);
    actions_.erase(actions_.begin() + static_cast<std::ptrdiff_t>(from));
    actions_.insert(actions_.begin() + static_cast<std::ptrdiff_t>(to), std::move(action));
}
bool CycleActionList::Contains(const CycleAction& action) const noexcept { return IndexOf(action) >= 0; }
std::ptrdiff_t CycleActionList::IndexOf(const CycleAction& action) const noexcept {
    auto found = std::find_if(actions_.begin(), actions_.end(), [&](const auto& item) {
        return item.ActionId == action.ActionId;
    });
    return found == actions_.end() ? -1 : std::distance(actions_.begin(), found);
}
void CycleActionList::Validate() const {
    if (!std::isfinite(cycleTime) || cycleTime < 1.0 || cycleTime > 3600.0)
        throw std::invalid_argument("cycleTime must be within 1..3600 seconds");
    std::unordered_set<std::string> identifiers;
    for (std::size_t index = 1; index < Count(); ++index) {
        ValidateAction(actions_[index]);
        if (!identifiers.insert(actions_[index].ActionId).second)
            throw std::invalid_argument("Duplicate ActionId");
    }
}

CycleScheduler::CycleScheduler(Clock clock) : clock_(std::move(clock)) {
    if (!clock_) clock_ = [] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    lastWall_ = clock_();
    if (!std::isfinite(lastWall_)) throw std::invalid_argument("Invalid clock");
}
CycleScheduler::~CycleScheduler() { Stop(); }
bool CycleScheduler::IsBlocked() const {
    return dispatchBlocking_ || std::any_of(active_.begin(), active_.end(), [](const auto& task) {
        return task->blocked;
    });
}
void CycleScheduler::AccountTime() {
    const double now = clock_();
    if (!std::isfinite(now) || now < lastWall_)
        throw std::logic_error("Cycle clock must be monotonic and finite");
    if (!paused_ && !IsBlocked()) logicalTime_ += now - lastWall_;
    lastWall_ = now;
}
void CycleScheduler::Diagnostic(const std::string& message) noexcept {
    if (onDiagnostic) try { onDiagnostic(message); } catch (...) { }
}
void CycleScheduler::SetDefinition(CycleActionList definition, std::uint64_t revision) {
    definition.Validate();
    if (revision <= pendingRevision_) throw std::invalid_argument("Definition revision must increase");
    pending_ = std::move(definition);
    pendingRevision_ = revision;
}
void CycleScheduler::Start(std::uint64_t generation, bool gameReady, bool chatReady) {
    if (lastGeneration_ && generation <= *lastGeneration_)
        throw std::invalid_argument("A restarted scheduler requires a new increasing generation");
    Stop();
    AccountTime();
    lastGeneration_ = generation;
    ticket_ = { generation, 0 };
    gameReady_ = gameReady;
    chatReady_ = chatReady;
    state_ = State::WaitingReady;
    if (gameReady_ && chatReady_) RequestBatch();
}
void CycleScheduler::SetReady(bool gameReady, bool chatReady) {
    gameReady_ = gameReady;
    chatReady_ = chatReady;
    if (state_ == State::WaitingReady && !paused_ && gameReady_ && chatReady_) RequestBatch();
}
void CycleScheduler::Cancel(Running& running) {
    if (running.cancelSent) return;
    running.cancelSent = true;
    running.cancellation->requested = true;
    if (running.task.cancel) try { running.task.cancel(); }
        catch (...) { Diagnostic("CycleAction cancellation callback failed: " + running.id); }
}
void CycleScheduler::Stop() {
    state_ = State::Stopped;
    if (batchCancellation_) batchCancellation_->requested = true;
    auto runningTasks = std::move(active_);
    active_.clear();
    for (auto& running : runningTasks) Cancel(*running);
    entries_.clear();
    responses_.clear();
    latestResponse_.reset();
    paused_ = false;
    dispatchBlocking_ = false;
}
void CycleScheduler::RequestBatch() {
    if (paused_ || state_ == State::Stopped) return;
    state_ = State::Collecting;
    ++ticket_.cycle;
    latestResponse_.reset();
    if (onBatchRequested) onBatchRequested(ticket_);
}
bool CycleScheduler::NotifyResponse(std::uint64_t generation) {
    if (state_ == State::Stopped || generation != ticket_.generation) return false;
    AccountTime();
    responses_.push_back({ ticket_, logicalTime_ });
    return true;
}
void CycleScheduler::DispatchResponses() {
    auto responses = std::move(responses_);
    responses_.clear();
    for (const auto& response : responses) {
        if (state_ == State::Stopped || response.ticket.generation != ticket_.generation) continue;
        if (response.ticket == ticket_) latestResponse_ = response.logicalTime;
        if (onResponse) try { onResponse(response.ticket); }
            catch (...) { Diagnostic("OnResponse callback failed"); }
        AccountTime();
    }
}
bool CycleScheduler::SubmitBatch(CycleBatch batch) {
    AccountTime();
    if (state_ != State::Collecting || paused_ || !(batch.ticket == ticket_)) return false;
    DispatchResponses();
    if (state_ != State::Collecting || paused_ || !(batch.ticket == ticket_)) return false;
    definition_ = pending_;
    appliedRevision_ = pendingRevision_;
    entries_.assign(definition_.Count(), {});
    cycleStart_ = logicalTime_;
    responseAnchor_ = latestResponse_.value_or(logicalTime_);
    hasResponse_ = batch.hasResponse;
    state_ = State::Executing;
    batchCancellation_ = std::make_shared<CancellationState>();
    CycleContext context{ ticket_, hasResponse_, batchCancellation_ };
    entries_[0].started = true;
    if (onCycle) try { onCycle(context); } catch (...) { Diagnostic("OnCycle callback failed"); }
    AccountTime();
    if (state_ != State::Executing || !(batch.ticket == ticket_) || paused_) return true;
    entries_[0].completed = logicalTime_;
    for (const auto& command : batch.commands) {
        if (command) try { command(context); } catch (...) { Diagnostic("Cycle command callback failed"); }
        AccountTime();
        if (state_ != State::Executing || !(batch.ticket == ticket_) || paused_) return true;
    }
    Tick();
    return true;
}
void CycleScheduler::PollTasks() {
    const auto snapshot = active_;
    for (const auto& running : snapshot) {
        if (std::find(active_.begin(), active_.end(), running) == active_.end()) continue;
        bool complete = false;
        try { complete = !running->task.poll || running->task.poll(); }
        catch (...) {
            complete = true;
            Cancel(*running);
            Diagnostic("CycleAction failed: " + running->id);
        }
        AccountTime();
        if (!complete) continue;
        if (running->ticket == ticket_ && running->index < entries_.size())
            entries_[running->index].completed = logicalTime_;
        auto found = std::find(active_.begin(), active_.end(), running);
        if (found != active_.end()) active_.erase(found);
    }
}
void CycleScheduler::StartReadyActions() {
    for (std::size_t index = 1; index < entries_.size(); ++index) {
        if (paused_ || state_ != State::Executing || IsBlocked()) return;
        if (logicalTime_ - cycleStart_ >= definition_.cycleTime) return;
        auto& entry = entries_[index];
        if (entry.started) continue;
        const auto action = definition_[index];
        if (!hasResponse_ && !action.isActionOnNoResponse) {
            entry.started = true;
            entry.completed = logicalTime_;
            continue;
        }
        double anchor = cycleStart_;
        if (action.reference == CycleAction::from::response) anchor = responseAnchor_;
        if (action.reference == CycleAction::from::prev) {
            if (!entries_[index - 1].completed) continue;
            anchor = *entries_[index - 1].completed;
        }
        if (logicalTime_ < anchor + action.timeFrom) continue;
        entry.started = true;
        if (std::any_of(active_.begin(), active_.end(), [&](const auto& running) {
            return running->id == action.ActionId;
        })) {
            entry.completed = logicalTime_;
            Diagnostic("CycleAction Busy: " + action.ActionId);
            continue;
        }
        const auto currentTicket = ticket_;
        auto cancellation = std::make_shared<CancellationState>();
        ActionTask task;
        dispatchBlocking_ = action.isCycleBlocked;
        try { task = action.invoke({ ticket_, hasResponse_, cancellation }); }
        catch (...) { Diagnostic("CycleAction invocation failed: " + action.ActionId); }
        AccountTime();
        dispatchBlocking_ = false;
        if (state_ != State::Executing || !(ticket_ == currentTicket)) {
            cancellation->requested = true;
            if (task.cancel) try { task.cancel(); } catch (...) { }
            return;
        }
        if (!task.poll) {
            entries_[index].completed = logicalTime_;
        } else {
            auto running = std::make_shared<Running>(Running{ action.ActionId, ticket_, index, std::move(task),
                cancellation, action.isCycleBlocked, action.isStoppedByCycle, false });
            active_.push_back(running);
            if (paused_) {
                cancellation->paused = true;
                if (running->task.pause) try { running->task.pause(true); }
                    catch (...) { Diagnostic("CycleAction pause callback failed: " + action.ActionId); }
            }
        }
    }
}
void CycleScheduler::EndCycle() {
    if (batchCancellation_) batchCancellation_->requested = true;
    const auto snapshot = active_;
    for (const auto& running : snapshot)
        if (running->ticket == ticket_ && running->stoppedByCycle) Cancel(*running);
    entries_.clear();
    RequestBatch();
}
void CycleScheduler::Tick() {
    if (inTick_ || state_ == State::Stopped) return;
    inTick_ = true;
    try {
        AccountTime();
        DispatchResponses(); // response handlers bypass ordinary-action blockers
        PollTasks();
        if (!paused_ && state_ == State::Executing) {
            StartReadyActions();
            if (!paused_ && state_ == State::Executing && !IsBlocked() &&
                logicalTime_ - cycleStart_ >= definition_.cycleTime) EndCycle();
        }
    } catch (...) { inTick_ = false; throw; }
    inTick_ = false;
}
void CycleScheduler::Pause() {
    if (paused_ || state_ == State::Stopped) return;
    AccountTime();
    paused_ = true;
    if (batchCancellation_) batchCancellation_->paused = true;
    const auto snapshot = active_;
    for (const auto& running : snapshot) {
        running->cancellation->paused = true;
        if (running->task.pause) try { running->task.pause(true); }
            catch (...) { Diagnostic("CycleAction pause callback failed: " + running->id); }
    }
}
void CycleScheduler::Resume() {
    if (!paused_ || state_ == State::Stopped) return;
    AccountTime();
    paused_ = false;
    const auto snapshot = active_;
    for (const auto& running : snapshot) {
        running->cancellation->paused = false;
        if (running->task.pause) try { running->task.pause(false); }
            catch (...) { Diagnostic("CycleAction resume callback failed: " + running->id); }
    }
    if (state_ == State::Stopped) return;
    if (state_ == State::Executing) EndCycle();
    else if (gameReady_ && chatReady_) RequestBatch();
    else state_ = State::WaitingReady;
}
double CycleScheduler::RemainingSeconds() {
    AccountTime();
    return state_ == State::Executing ? std::max(0.0, definition_.cycleTime - (logicalTime_ - cycleStart_)) : 0.0;
}

} // namespace Rice::Streaming
