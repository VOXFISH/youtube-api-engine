#include "YouTubeListener.h"

#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace Rice {
namespace {
void ValidateConfig(const Streaming::ListenerBindingConfig& row) {
    if (row.bindingId.empty() || row.functionId.empty() || row.pattern.empty())
        throw std::invalid_argument("Listener binding requires ID, function and pattern");
    if (row.order > 1000000) throw std::invalid_argument("Listener binding order is out of range");
    if (row.permission != "LocalAdmin" && row.permission != "Everyone" && row.permission != "Owner" &&
        row.permission != "OwnerOrModerator" && row.permission != "ChannelAllowlist")
        throw std::invalid_argument("Unknown command permission");
    if (row.allowedChannelIds.size() > 1024) throw std::invalid_argument("Too many allowed channels");
    for (const auto& channel : row.allowedChannelIds)
        if (channel.empty() || channel.front() == '@' || channel.size() > 256 || channel.find_first_of(" \r\n\t") != std::string::npos)
            throw std::invalid_argument("Allowlist requires stable channel IDs");
    for (const auto* text : {&row.bindingId, &row.functionId, &row.pattern}) {
        if (text->size() > 16384 || text->find_first_of("\r\n\0", 0, 3) != std::string::npos)
            throw std::invalid_argument("Listener binding text must fit one metadata line");
    }
}
}
YouTubeListener::YouTubeListener() : lifetime_(std::make_shared<Streaming::ListenerLifetime>()) {
    lifetime_->listener = this;
}
YouTubeListener::~YouTubeListener() { OnDestroy(); }
void YouTubeListener::OnDestroy() { lifetime_->listener = nullptr; }
void YouTubeListener::AddBinding(Streaming::CommandBinding binding) {
    if (binding.bindingId.empty() || !binding.invoke || binding.pattern.Function().functionId.empty())
        throw std::invalid_argument("Listener requires a compiled native binding");
    auto found = std::find_if(registered_.begin(), registered_.end(), [&](const auto& existing) {
        return existing.bindingId == binding.bindingId;
    });
    if (found != registered_.end()) throw std::invalid_argument("Duplicate listener BindingId");
    registered_.push_back(std::move(binding));
}
std::string YouTubeListener::EncodeBindings(const std::vector<Streaming::ListenerBindingConfig>& configuration) {
    if (configuration.size() > 4096) throw std::invalid_argument("Too many listener bindings");
    std::ostringstream output;
    output << 3 << ' ' << configuration.size();
    std::unordered_set<std::string> ids;
    for (const auto& row : configuration) {
        ValidateConfig(row);
        if (!ids.insert(row.bindingId).second) throw std::invalid_argument("Duplicate configured BindingId");
        output << ' ' << std::quoted(row.bindingId) << ' ' << std::quoted(row.functionId) << ' '
               << std::quoted(row.pattern) << ' ' << (row.enabled ? 1 : 0) << ' ' << row.order << ' ' << std::quoted(row.permission);
        output << ' ' << row.allowedChannelIds.size();
        for (const auto& channel : row.allowedChannelIds) output << ' ' << std::quoted(channel);
    }
    if (output.tellp() > 1048576) throw std::invalid_argument("Listener configuration exceeds one MiB");
    return output.str();
}
std::vector<Streaming::ListenerBindingConfig> YouTubeListener::DecodeBindings(const std::string& serialized) {
    if (serialized.empty()) return {};
    if (serialized.size() > 1048576) throw std::invalid_argument("Listener configuration exceeds one MiB");
    std::istringstream input(serialized);
    unsigned version = 0;
    std::size_t count = 0;
    if (!(input >> version >> count) || version < 1 || version > 3 || count > 4096)
        throw std::invalid_argument("Invalid listener configuration header");
    std::vector<Streaming::ListenerBindingConfig> rows;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < count; ++index) {
        Streaming::ListenerBindingConfig row;
        int enabled = 0;
        if (!(input >> std::quoted(row.bindingId) >> std::quoted(row.functionId) >> std::quoted(row.pattern) >> enabled >> row.order) ||
            (enabled != 0 && enabled != 1)) throw std::invalid_argument("Invalid listener binding record");
        row.enabled = enabled != 0;
        if (version >= 2 && !(input >> std::quoted(row.permission))) throw std::invalid_argument("Missing command permission");
        if (version >= 3) {
            std::size_t channelCount = 0;
            if (!(input >> channelCount) || channelCount > 1024) throw std::invalid_argument("Invalid channel allowlist");
            for (std::size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
                std::string channel;
                if (!(input >> std::quoted(channel))) throw std::invalid_argument("Missing allowed channel");
                row.allowedChannelIds.push_back(std::move(channel));
            }
        }
        ValidateConfig(row);
        if (!ids.insert(row.bindingId).second) throw std::invalid_argument("Duplicate configured BindingId");
        rows.push_back(std::move(row));
    }
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("Trailing listener configuration data");
    return rows;
}
void YouTubeListener::SetBindingConfiguration(const std::vector<Streaming::ListenerBindingConfig>& configuration) {
    commandBindings = EncodeBindings(configuration);
}
std::vector<Streaming::CommandBinding> YouTubeListener::BuildBindings(std::vector<std::string>* diagnostics) const {
    std::vector<Streaming::ListenerBindingConfig> overrides;
    try { overrides = DecodeBindings(commandBindings); }
    catch (const std::exception& error) {
        if (diagnostics) diagnostics->push_back(error.what());
        return {};
    }
    struct Ordered { std::size_t order; Streaming::CommandBinding binding; };
    std::vector<Ordered> ordered;
    for (std::size_t index = 0; index < registered_.size(); ++index) {
        const auto& binding = registered_[index];
        if (std::any_of(overrides.begin(), overrides.end(), [&](const auto& row) { return row.bindingId == binding.bindingId; })) continue;
        ordered.push_back({ index, binding });
    }
    for (const auto& row : overrides) {
        auto native = std::find_if(registered_.begin(), registered_.end(), [&](const auto& binding) {
            return binding.pattern.Function().functionId == row.functionId;
        });
        if (native == registered_.end()) {
            if (diagnostics) diagnostics->push_back("Unregistered native command function: " + row.functionId);
            continue;
        }
        try {
            auto binding = *native;
            binding.bindingId = row.bindingId;
            binding.pattern = native->pattern.WithPattern(row.pattern);
            binding.enabled = row.enabled;
            binding.permission = row.permission;
            binding.allowedChannelIds = row.allowedChannelIds;
            ordered.push_back({ row.order, std::move(binding) });
        } catch (const std::exception& error) {
            if (diagnostics) diagnostics->push_back("Invalid binding " + row.bindingId + ": " + error.what());
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) { return left.order < right.order; });
    std::vector<Streaming::CommandBinding> result;
    for (auto& item : ordered) result.push_back(std::move(item.binding));
    return result;
}
} // namespace Rice

namespace Rice::Streaming {
namespace {
YouTubeListener* Resolve(const std::weak_ptr<ListenerLifetime>& weak, std::uint64_t scene,
    EntityID objectId, std::uint64_t poolGeneration) {
    auto state = weak.lock();
    if (!state || !state->listener || !state->listener->enabled) return nullptr;
    auto* listener = state->listener;
    auto& object = listener->GetGameObject();
    if (object.GetID() != objectId || object.GetScene().GetInstanceID() != scene ||
        object.GetPoolGeneration() != poolGeneration || !object.IsActive() ||
        object.GetComponent<YouTubeListener>() != listener) return nullptr;
    return listener;
}
std::string MessageKey(const RoutedYouTubeCommand& command) {
    return std::to_string(command.context.liveChatId.size()) + ":" + command.context.liveChatId + command.context.messageId;
}
}
GameCommandReceiver::GameCommandReceiver(std::string instanceId, std::uint64_t generation,
    std::uint64_t sceneInstanceId, std::string mainSlotId, std::size_t capacity, std::size_t dedupCapacity)
    : instanceId_(std::move(instanceId)), generation_(generation), sceneInstanceId_(sceneInstanceId),
      mainSlotId_(std::move(mainSlotId)), capacity_(capacity), dedupCapacity_(dedupCapacity) {
    if (instanceId_.empty() || mainSlotId_.empty() || !capacity_ || !dedupCapacity_)
        throw std::invalid_argument("Receiver requires instance, main slot and positive queue bounds");
}
void GameCommandReceiver::Attach(YouTubeListener& listener) {
    auto& object = listener.GetGameObject();
    if (object.GetScene().GetInstanceID() != sceneInstanceId_)
        throw std::invalid_argument("Listener belongs to another scene instance");
    auto lifetime = listener.Lifetime().lock();
    std::erase_if(attached_, [](const auto& item) { return item.lifetime.expired(); });
    for (auto& attached : attached_) if (attached.lifetime.lock() == lifetime) {
        attached.poolGeneration = object.GetPoolGeneration();
        return;
    }
    attached_.push_back({ lifetime, object.GetID(), object.GetPoolGeneration() });
}
void GameCommandReceiver::SetActiveSlots(std::vector<std::string> slots) {
    activeSlots_.clear();
    for (auto& slot : slots) if (!slot.empty()) activeSlots_.insert(std::move(slot));
}
void GameCommandReceiver::SetMainSlot(std::string slotId) {
    if (slotId.empty()) throw std::invalid_argument("Main slot cannot be empty");
    mainSlotId_ = std::move(slotId);
}
void GameCommandReceiver::Pause() { paused_ = true; pending_.clear(); ++inputEpoch_; }
void GameCommandReceiver::SetSlotMuted(const std::string& slot,bool muted,std::int64_t cutoff) {
    if(slot.empty())throw std::invalid_argument("Slot ID is required");
    auto& gate=slotGates_[slot];
    if(gate.muted==muted && cutoff<=gate.cutoff)return;
    gate.muted=muted;++gate.epoch;gate.cutoff=std::max(gate.cutoff,cutoff);
    std::erase_if(pending_,[&](const auto& command){return command.context.originSlotId==slot;});
}
std::uint64_t GameCommandReceiver::SlotInputEpoch(const std::string& slot) const {
    const auto found=slotGates_.find(slot);return found==slotGates_.end()?0:found->second.epoch;
}
void GameCommandReceiver::Resume(std::int64_t acceptPublishedAfterUnixMs) {
    paused_ = false;
    pending_.clear();
    ++inputEpoch_;
    publishedCutoff_ = std::max(publishedCutoff_, acceptPublishedAfterUnixMs);
}
bool GameCommandReceiver::Valid(const RoutedYouTubeCommand& command) const {
    const auto found=slotGates_.find(command.context.originSlotId);
    if(found!=slotGates_.end() && (found->second.muted || command.slotInputEpoch!=found->second.epoch ||
        command.publishedAtUnixMs<=found->second.cutoff))return false;
    return !paused_ && command.context.instanceId == instanceId_ && command.context.generation == generation_ &&
        command.inputEpoch == inputEpoch_ && activeSlots_.contains(command.context.originSlotId) &&
        !command.context.liveChatId.empty() && !command.context.messageId.empty() &&
        command.publishedAtUnixMs > publishedCutoff_;
}
void GameCommandReceiver::Remember(const std::string& key) {
    if (!seen_.insert(key).second) return;
    seenOrder_.push_back(key);
    while (seenOrder_.size() > dedupCapacity_) { seen_.erase(seenOrder_.front()); seenOrder_.pop_front(); }
}
void GameCommandReceiver::Diagnostic(std::string message) {
    if (diagnostics_.size() >= 256) diagnostics_.erase(diagnostics_.begin());
    diagnostics_.push_back(std::move(message));
}
bool GameCommandReceiver::Enqueue(RoutedYouTubeCommand command) {
    if (!Valid(command)) return false;
    const auto key = MessageKey(command);
    if (seen_.contains(key)) return false;
    Remember(key);
    if (pending_.size() >= capacity_) { Diagnostic("YouTube command queue capacity exceeded"); return false; }
    command.context.mainSlotId = mainSlotId_;
    pending_.push_back(std::move(command));
    return true;
}
std::vector<RoutedYouTubeCommand> GameCommandReceiver::TakeBatch() {
    if (paused_) { pending_.clear(); return {}; }
    auto result = std::move(pending_);
    pending_.clear();
    return result;
}
CommandDispatchResult GameCommandReceiver::Dispatch(const RoutedYouTubeCommand& command) {
    if (!Valid(command)) return {};
    const auto key = MessageKey(command);
    if (!dispatched_.insert(key).second) return {};
    dispatchOrder_.push_back(key);
    while (dispatchOrder_.size() > dedupCapacity_) { dispatched_.erase(dispatchOrder_.front()); dispatchOrder_.pop_front(); }
    CommandRegistry registry;
    for (const auto& attached : attached_) {
        auto* listener = Resolve(attached.lifetime, sceneInstanceId_, attached.objectId, attached.poolGeneration);
        if (!listener) continue;
        std::vector<std::string> errors;
        auto bindings = listener->BuildBindings(&errors);
        for (auto& error : errors) Diagnostic(std::move(error));
        for (auto& binding : bindings) {
            auto nativeAuthorization = std::move(binding.authorized);
            binding.authorized = [evaluate = authorize, permission = binding.permission, allowedChannels = binding.allowedChannelIds,
                nativeAuthorization = std::move(nativeAuthorization)](const CommandContext& context) {
                return evaluate && evaluate(context, permission, allowedChannels) && (!nativeAuthorization || nativeAuthorization(context));
            };
            auto original = std::move(binding.available);
            binding.available = [attached, scene = sceneInstanceId_, original = std::move(original)] {
                return Resolve(attached.lifetime, scene, attached.objectId, attached.poolGeneration) && (!original || original());
            };
            binding.bindingId = std::to_string(attached.objectId) + ":" + binding.bindingId;
            registry.Add(std::move(binding));
        }
    }
    auto context = command.context;
    context.mainSlotId = mainSlotId_;
    auto result = registry.Dispatch(command.text, context);
    for (const auto& message : result.diagnostics) Diagnostic(message);
    return result;
}
} // namespace Rice::Streaming
