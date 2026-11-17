#include "Streaming/YouTubeListener.h"

#include <iostream>
#include <stdexcept>

using namespace Rice;
using namespace Rice::Streaming;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct MoveScript : Component {
    int direction = 0, steps = 0, calls = 0;
    void Move(int nextDirection, int nextSteps) { direction = nextDirection; steps = nextSteps; ++calls; }
};
void Bind(YouTubeListener& listener) {
    listener.BindNative<&MoveScript::Move>("move", "MoveScript.Move", "!회피 {direction : (좌, -1), (우, 1)} {steps}칸",
        std::tuple{Parameter<int>("direction"), Parameter<int>("steps")});
}
RoutedYouTubeCommand Message(const GameCommandReceiver& receiver, std::string messageId, std::int64_t published = 1000) {
    RoutedYouTubeCommand command;
    command.text = "!회피 좌 3칸";
    command.context.instanceId = "instance";
    command.context.generation = 4;
    command.context.originSlotId = "secondary";
    command.context.liveChatId = "chat";
    command.context.messageId = std::move(messageId);
    command.context.authorChannelId = "verified";
    command.inputEpoch = receiver.InputEpoch();
    command.publishedAtUnixMs = published;
    return command;
}
void RoutingPauseAndLifetime() {
    auto scene = std::make_unique<Scene>();
    auto& object = scene->CreateGameObject("controlled");
    auto& script = object.AddComponent<MoveScript>();
    auto& listener = object.AddComponent<YouTubeListener>();
    Bind(listener);
    GameCommandReceiver receiver("instance", 4, scene->GetInstanceID(), "main", 2, 8);
    receiver.SetActiveSlots({"main", "secondary"}); receiver.Attach(listener);
    auto denied = receiver.Dispatch(Message(receiver, "no-authorizer"));
    Check(denied.items.size() == 1 && denied.items[0].status == CommandDispatchStatus::Denied && script.calls == 0,
        "Transport receiver denies commands when no authorization evaluator is installed");
    receiver.authorize = [](const auto&, const auto&, const auto&) { return true; };
    auto command = Message(receiver, "one");
    Check(receiver.Enqueue(command) && !receiver.Enqueue(command), "Cross-slot source messages deduplicate by live chat and message ID");
    auto batch = receiver.TakeBatch();
    Check(batch.size() == 1 && batch[0].context.mainSlotId == "main", "Secondary input routes through main-slot data context");
    auto result = receiver.Dispatch(batch[0]);
    Check(result.items.size() == 1 && script.calls == 1 && script.direction == -1 && script.steps == 3, "Whole-game receiver reaches native member");
    Check(receiver.Dispatch(batch[0]).items.empty() && script.calls == 1, "Accidental batch replay cannot execute the same message twice");
    receiver.Pause();
    Check(!receiver.Enqueue(Message(receiver, "paused")), "Paused input dropped");
    receiver.Resume(1500);
    Check(receiver.Dispatch(batch[0]).items.empty(), "Detached pre-pause batch cannot replay");
    Check(!receiver.Enqueue(Message(receiver, "late-paused", 1400)), "Late API result posted during pause is dropped");
    auto resumed = Message(receiver, "resumed", 1600);
    Check(receiver.Enqueue(resumed), "New post-resume command accepted");
    object.RemoveComponent<MoveScript>();
    result = receiver.Dispatch(resumed);
    Check(result.items[0].status == CommandDispatchStatus::Unavailable, "Removed component never invoked before flush");
    object.FlushComponents();
    object.AddComponent<MoveScript>();
    object.RemoveComponent<YouTubeListener>();
    Check(receiver.Dispatch(resumed).items.empty(), "Removed listener is excluded before its destructor");
    object.FlushComponents();
    scene.reset();
    Check(receiver.Dispatch(resumed).items.empty(), "Receiver safely outlives destroyed scene through weak listener lifetime");
}
void ReplacementSlotMute() {
    Scene scene;GameCommandReceiver receiver("instance",4,scene.GetInstanceID(),"main");
    receiver.SetActiveSlots({"main","secondary"});
    auto stale=Message(receiver,"before");
    Check(receiver.Enqueue(stale),"Pre-replacement command queued");
    receiver.SetSlotMuted("secondary",true,1100);
    Check(receiver.PendingCount()==0 && !receiver.Enqueue(Message(receiver,"during",1200)),"Waiting slot input discarded");
    auto other=Message(receiver,"other",1200);other.context.originSlotId="main";
    Check(receiver.Enqueue(other),"Other slot continues without game pause");
    receiver.SetSlotMuted("secondary",false,1500);
    stale.context.messageId="late-pre-replacement";stale.publishedAtUnixMs=1600;
    Check(!receiver.Enqueue(stale),"Late old epoch cannot replay after unmute");
    auto late=Message(receiver,"during-late",1400);late.slotInputEpoch=receiver.SlotInputEpoch("secondary");
    Check(!receiver.Enqueue(late),"Waiting-period API result rejected by publication cutoff");
    auto fresh=Message(receiver,"after",1600);fresh.slotInputEpoch=receiver.SlotInputEpoch("secondary");
    Check(receiver.Enqueue(fresh),"Fresh post-replacement command accepted");
}
void ConfigAndPoolGenerations() {
    Scene scene;
    auto& object = scene.CreateGameObject();
    auto& script = object.AddComponent<MoveScript>();
    auto& listener = object.AddComponent<YouTubeListener>();
    Bind(listener);
    listener.SetBindingConfiguration({ {"move", "MoveScript.Move", "!move {steps} {direction}", true, 2} });
    auto decoded = YouTubeListener::DecodeBindings(listener.commandBindings);
    Check(decoded.size() == 1 && decoded[0].pattern == "!move {steps} {direction}", "Versioned metadata roundtrip");
    GameCommandReceiver receiver("instance", 4, scene.GetInstanceID(), "main");
    receiver.authorize = [](const auto&, const auto&, const auto&) { return true; };
    receiver.SetActiveSlots({"secondary"}); receiver.Attach(listener);
    auto command = Message(receiver, "override"); command.text = "!move 5 1";
    receiver.Dispatch(command);
    Check(script.calls == 1 && script.steps == 5 && script.direction == 1, "Inspector metadata overrides native pattern by stable BindingId");
    object.SetPoolState(true, true); object.SetPoolState(true, false);
    command.context.messageId = "old-pool";
    Check(receiver.Dispatch(command).items.empty(), "Old receiver attachment cannot target recycled pool generation");
    receiver.Attach(listener);
    command.context.messageId = "new-pool";
    receiver.Dispatch(command);
    Check(script.calls == 2, "Explicit pool reacquire attachment targets current generation");
    listener.commandBindings = "broken";
    command.context.messageId = "broken-config";
    Check(receiver.Dispatch(command).items.empty() && !receiver.Diagnostics().empty(), "Corrupt metadata does not silently execute native defaults");
    bool rejected = false;
    try { YouTubeListener::DecodeBindings("1 0 unexpected"); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "Trailing config data rejected");
}
void QueueAndSlotLimits() {
    Scene scene;
    GameCommandReceiver receiver("instance", 4, scene.GetInstanceID(), "main", 1, 2);
    receiver.SetActiveSlots({"secondary"});
    Check(receiver.Enqueue(Message(receiver, "one")), "First bounded input accepted");
    Check(!receiver.Enqueue(Message(receiver, "two")) && receiver.PendingCount() == 1 && !receiver.Diagnostics().empty(), "Queue bound drops overflow with diagnostic");
    auto foreign = Message(receiver, "foreign"); foreign.context.generation = 3;
    Check(!receiver.Enqueue(foreign), "Wrong game generation rejected");
    receiver.SetActiveSlots({});
    Check(receiver.Dispatch(receiver.TakeBatch()[0]).items.empty(), "Detached source slot cannot dispatch already queued command");
}
}
int main() {
    try {
        RoutingPauseAndLifetime(); ConfigAndPoolGenerations(); QueueAndSlotLimits(); ReplacementSlotMute();
        std::cout << "YouTube listener tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "YouTube listener test failed: " << error.what() << '\n';
        return 1;
    }
}
