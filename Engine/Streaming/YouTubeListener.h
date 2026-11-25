#pragma once

#include "NativeCommand.h"
#include "Scene/Scene.h"

#include <deque>
#include <limits>
#include <unordered_set>
#include <unordered_map>

namespace Rice { class YouTubeListener; }
namespace Rice::Streaming {

struct ListenerLifetime { YouTubeListener* listener = nullptr; };
struct ListenerBindingConfig {
    std::string bindingId;
    std::string functionId;
    std::string pattern;
    bool enabled = true;
    std::size_t order = 0;
    std::string permission = "LocalAdmin";
    std::vector<std::string> allowedChannelIds;
    bool operator==(const ListenerBindingConfig&) const = default;
};
struct RoutedYouTubeCommand {
    std::string text;
    CommandContext context;
    std::uint64_t inputEpoch = 0;
    std::int64_t publishedAtUnixMs = 0;
    std::uint64_t slotInputEpoch = 0;
};

// The whole-game receiver owns one queue and routing boundary for an instance.
// Transport workers marshal Enqueue onto the game owner thread.
class GameCommandReceiver {
public:
    GameCommandReceiver(std::string instanceId, std::uint64_t generation,
        std::uint64_t sceneInstanceId, std::string mainSlotId,
        std::size_t capacity = 1024, std::size_t dedupCapacity = 4096);
    void Attach(YouTubeListener& listener);
    void SetActiveSlots(std::vector<std::string> slots);
    void SetMainSlot(std::string slotId);
    void SetSlotMuted(const std::string& slotId,bool muted,std::int64_t acceptPublishedAfterUnixMs);
    std::uint64_t SlotInputEpoch(const std::string& slotId) const;
    void Pause();
    void Resume(std::int64_t acceptPublishedAfterUnixMs);
    bool Enqueue(RoutedYouTubeCommand command);
    std::vector<RoutedYouTubeCommand> TakeBatch();
    CommandDispatchResult Dispatch(const RoutedYouTubeCommand& command);
    std::uint64_t InputEpoch() const noexcept { return inputEpoch_; }
    std::size_t PendingCount() const noexcept { return pending_.size(); }
    const std::vector<std::string>& Diagnostics() const noexcept { return diagnostics_; }
    void ClearDiagnostics() { diagnostics_.clear(); }
    // No evaluator means deny, including when low-level bindings omit a callback.
    std::function<bool(const CommandContext&, const std::string&, const std::vector<std::string>&)> authorize;
private:
    struct Attached {
        std::weak_ptr<ListenerLifetime> lifetime;
        EntityID objectId = 0;
        std::uint64_t poolGeneration = 0;
    };
    std::string instanceId_;
    std::uint64_t generation_ = 0;
    std::uint64_t sceneInstanceId_ = 0;
    std::string mainSlotId_;
    std::unordered_set<std::string> activeSlots_;
    struct SlotGate { bool muted=false;std::uint64_t epoch=0;std::int64_t cutoff=std::numeric_limits<std::int64_t>::min(); };
    std::unordered_map<std::string,SlotGate> slotGates_;
    std::vector<Attached> attached_;
    std::vector<RoutedYouTubeCommand> pending_;
    std::unordered_set<std::string> seen_;
    std::deque<std::string> seenOrder_;
    std::unordered_set<std::string> dispatched_;
    std::deque<std::string> dispatchOrder_;
    std::vector<std::string> diagnostics_;
    std::size_t capacity_;
    std::size_t dedupCapacity_;
    std::uint64_t inputEpoch_ = 1;
    std::int64_t publishedCutoff_ = std::numeric_limits<std::int64_t>::min();
    bool paused_ = false;
    bool Valid(const RoutedYouTubeCommand& command) const;
    void Remember(const std::string& key);
    void Diagnostic(std::string message);
};

} // namespace Rice::Streaming

namespace Rice {

class YouTubeListener : public Component {
public:
    YouTubeListener();
    ~YouTubeListener() override;
    void OnDestroy() override;
    // Single-line versioned metadata is persisted by SceneCodec. No addresses
    // or invocation closures are serialized; native functions register at load.
    std::string commandBindings;
    void AddBinding(Streaming::CommandBinding binding);
    const std::vector<Streaming::CommandBinding>& RegisteredBindings() const noexcept { return registered_; }
    std::vector<Streaming::CommandBinding> BuildBindings(std::vector<std::string>* diagnostics = nullptr) const;
    void SetBindingConfiguration(const std::vector<Streaming::ListenerBindingConfig>& configuration);
    static std::string EncodeBindings(const std::vector<Streaming::ListenerBindingConfig>& configuration);
    static std::vector<Streaming::ListenerBindingConfig> DecodeBindings(const std::string& serialized);
    std::weak_ptr<Streaming::ListenerLifetime> Lifetime() const noexcept { return lifetime_; }

    template<auto Method, class... Parameters>
    void BindNative(std::string bindingId, std::string functionId, std::string pattern,
        std::tuple<Parameters...> parameters, Streaming::NativeCommandOptions options = {}) {
        using Target = typename Streaming::NativeCommandDetail::Traits<decltype(Method)>::Class;
        static_assert(std::is_base_of_v<Component, Target>, "Listener member targets must be components on its object");
        auto weak = Lifetime();
        auto resolve = [weak]() -> Target* {
            auto state = weak.lock();
            if (!state || !state->listener || !state->listener->enabled) return nullptr;
            auto& object = state->listener->GetGameObject();
            if (!object.IsActive() || object.GetComponent<YouTubeListener>() != state->listener) return nullptr;
            auto* target = object.GetComponent<Target>();
            return target && target->enabled ? target : nullptr;
        };
        AddBinding(Streaming::MakeNativeMemberBinding<Method>(std::move(bindingId), std::move(functionId),
            std::move(pattern), std::move(resolve), std::move(parameters), std::move(options)));
    }
private:
    std::shared_ptr<Streaming::ListenerLifetime> lifetime_;
    std::vector<Streaming::CommandBinding> registered_;
};

} // namespace Rice
