#include "Streaming/CycleScheduler.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Rice::Streaming;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Near(double value, double expected, const char* message) { Check(std::abs(value - expected) < 0.000001, message); }
template<class F> void Reject(F&& fn, const char* message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, message);
}
CycleAction Action(std::string id, std::function<ActionTask(const CycleContext&)> invoke) {
    CycleAction result; result.ActionId = std::move(id); result.invoke = std::move(invoke); return result;
}
void Budget(bool blocked) {
    double now = 0;
    int requests = 0;
    CycleScheduler scheduler([&] { return now; });
    CycleActionList definition;
    definition.cycleTime = 10;
    auto work = Action("work", [&](const auto&) { now += 3; return ActionTask{}; });
    work.isCycleBlocked = blocked;
    definition.Add(work);
    scheduler.SetDefinition(definition, 1);
    scheduler.onBatchRequested = [&](auto) { ++requests; };
    scheduler.Start(1, false, true);
    Check(requests == 0, "First cycle waits for both readiness gates");
    scheduler.SetReady(true, true);
    Check(requests == 1, "First query has no leading cycle delay");
    Check(scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} }), "Batch accepted");
    Near(scheduler.RemainingSeconds(), blocked ? 10.0 : 7.0, "10-second user budget semantics");
    now += blocked ? 9.5 : 6.5;
    scheduler.Tick();
    Check(requests == 1, "No early cycle completion");
    now += 0.5;
    scheduler.Tick();
    Check(requests == 2, "Next query begins at budget boundary");
    now += 20; // network wait is outside the next cycle's budget
    auto empty = definition;
    empty.Clear();
    scheduler.SetDefinition(empty, 2);
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} });
    Near(scheduler.RemainingSeconds(), 10, "Fetch delay does not consume next cycle budget");
}
void ListRules() {
    Rice::List<Rice::CycleAction> list;
    auto first = Action("first", [](const auto&) { return ActionTask{}; });
    auto second = Action("second", [](const auto&) { return ActionTask{}; });
    list.Add(first); list.Insert(1, second);
    Check(list.Count() == 3 && list.IndexOf(second) == 1, "Insert and IndexOf");
    list.Move(1, 2);
    Check(list.Indexof(first) == 1 && list.IndexOf(second) == 2, "Move and compatibility Indexof");
    Reject([&] { list.RemoveAt(0); }, "Cannot delete OnCycle");
    Reject([&] { list.Move(1, 0); }, "Cannot move before OnCycle");
    Reject([&] { list.Add(first); }, "Cannot reuse ActionId");
    list.Clear();
    Check(list.Count() == 1 && !list.Contains(first), "Clear preserves reserved row zero");
    list.cycleTime = 0;
    Reject([&] { list.Validate(); }, "Invalid cycle length rejected");
}
void ResponsePriorityAndPrev() {
    double now = 0;
    bool complete = false;
    std::vector<std::string> order;
    CycleScheduler scheduler([&] { return now; });
    CycleActionList definition; definition.cycleTime = 10;
    auto blocker = Action("blocker", [&](const auto&) {
        order.push_back("blocker");
        return ActionTask{ [&] { order.push_back("poll"); return complete; }, {}, {} };
    });
    blocker.isCycleBlocked = true;
    definition.Add(blocker);
    auto next = Action("next", [&](const auto&) { order.push_back("next"); return ActionTask{}; });
    next.From(CycleAction::from::prev); next.timeFrom = 2;
    definition.Add(next);
    scheduler.onCycle = [&](const auto&) { order.push_back("cycle"); };
    scheduler.onResponse = [&](auto) { order.push_back("response"); };
    scheduler.SetDefinition(definition, 1);
    scheduler.Start(7, true, true);
    scheduler.NotifyResponse(7);
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} });
    Check(order == std::vector<std::string>{"response", "cycle", "blocker"}, "Response then OnCycle then list");
    order.clear(); now = 3;
    scheduler.NotifyResponse(7);
    scheduler.Tick();
    Check(order == std::vector<std::string>{"response", "poll"}, "Response bypasses blocker and precedes polling");
    Near(scheduler.RemainingSeconds(), 10, "Async blocking freezes budget");
    complete = true; scheduler.Tick();
    now = 4.5; scheduler.Tick();
    Check(order.back() != "next", "prev offset waits after completion");
    now = 5; scheduler.Tick();
    Check(order.back() == "next", "prev offset begins at completion");
}
void RevisionAndNoResponse() {
    double now = 0;
    std::vector<std::string> calls;
    CycleScheduler scheduler([&] { return now; });
    CycleActionList first; first.cycleTime = 2;
    auto skip = Action("skip", [&](const auto&) { calls.push_back("bad"); return ActionTask{}; });
    skip.isActionOnNoResponse = false; first.Add(skip);
    auto fallback = Action("fallback", [&](const auto&) { calls.push_back("fallback"); return ActionTask{}; });
    fallback.From(CycleAction::from::response); first.Add(fallback);
    scheduler.onCycle = [&](const auto& context) { calls.push_back(context.hasResponse ? "cycle-with-response" : "cycle-no-response"); };
    scheduler.SetDefinition(first, 1); scheduler.Start(1, true, true);
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), false, {} });
    Check(calls == std::vector<std::string>{"cycle-no-response", "fallback"}, "No-response gating keeps OnCycle and permitted actions");
    CycleActionList next; next.cycleTime = 10;
    scheduler.SetDefinition(next, 2);
    Check(scheduler.AppliedRevision() == 1, "Revision does not change current snapshot");
    now = 2; scheduler.Tick();
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} });
    Check(scheduler.AppliedRevision() == 2 && calls.back() == "cycle-with-response", "Revision applies to next batch; empty success is response");
    Near(scheduler.RemainingSeconds(), 10, "Next cycle gets new length");
    Reject([&] { scheduler.SetDefinition(next, 2); }, "Stale revision rejected");
}
void CancellationBusyAndPause() {
    double now = 0;
    int cancellableStarts = 0, survivorStarts = 0, cancels = 0, commands = 0;
    std::vector<std::string> diagnostics;
    CycleScheduler scheduler([&] { return now; });
    CycleActionList list; list.cycleTime = 1;
    auto cancellation = Action("cancel", [&](const auto&) {
        ++cancellableStarts;
        return ActionTask{ [] { return false; }, [&] { ++cancels; }, {} };
    });
    cancellation.isStoppedByCycle = true;
    auto survivor = Action("survivor", [&](const auto&) {
        ++survivorStarts;
        return ActionTask{ [] { return false; }, [&] { ++cancels; }, {} };
    });
    list.Add(cancellation); list.Add(survivor);
    scheduler.onDiagnostic = [&](const auto& message) { diagnostics.push_back(message); };
    scheduler.SetDefinition(list, 1); scheduler.Start(1, true, true);
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} });
    now = 1; scheduler.Tick();
    Check(cancels == 1, "Cycle end cancels only flagged tasks");
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} });
    Check(cancellableStarts == 1 && survivorStarts == 1 && diagnostics.size() == 2, "Unfinished action IDs cannot reenter");
    const auto old = scheduler.CurrentTicket();
    scheduler.Pause(); now = 100;
    Check(!scheduler.SubmitBatch({ old, true, { [&](const auto&) { ++commands; } } }), "Paused commands dropped");
    scheduler.Resume();
    Check(!(old == scheduler.CurrentTicket()), "Resume invalidates pre-pause batch ticket");
    Check(!scheduler.SubmitBatch({ old, true, { [&](const auto&) { ++commands; } } }), "Old batch cannot replay after resume");
    scheduler.Stop();
    Check(cancels == 2 && commands == 0, "Stop cancels surviving work exactly once");
    scheduler.Start(2, true, true);
    Check(!scheduler.NotifyResponse(1), "Old-generation API event rejected");
    Check(!scheduler.SubmitBatch({ old, true, {} }), "Old-generation batch rejected");
    Reject([&] { scheduler.Start(2, true, true); }, "Reusing generation cannot revive stale tickets");
}
void PauseWithinCommandAndStopWithinTask() {
    double now = 0;
    int calls = 0;
    CycleScheduler scheduler([&] { return now; });
    scheduler.Start(1, true, true);
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {
        [&](const auto&) { ++calls; scheduler.Pause(); },
        [&](const auto&) { ++calls; }
    }});
    Check(calls == 1, "Pause command suppresses rest of same batch");
    scheduler.Resume();
    CycleActionList list;
    list.Add(Action("stop-in-poll", [&](const auto&) {
        return ActionTask{ [&] { scheduler.Stop(); return true; }, {}, {} };
    }));
    scheduler.SetDefinition(list, 1);
    scheduler.SubmitBatch({ scheduler.CurrentTicket(), true, {} });
    scheduler.Tick();
    Check(scheduler.GetState() == CycleScheduler::State::Stopped && scheduler.ActiveTaskCount() == 0,
        "Task may stop scheduler safely without invalid iterators");
}
}
int main() {
    try {
        ListRules(); Budget(false); Budget(true); ResponsePriorityAndPrev();
        RevisionAndNoResponse(); CancellationBusyAndPause(); PauseWithinCommandAndStopWithinTask();
        std::cout << "Streaming cycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Streaming cycle test failed: " << error.what() << '\n';
        return 1;
    }
}
