#pragma once

#include "Streaming/CycleDefinition.h"
#include "Streaming/YouTubeListener.h"
#include "Serialization/Json.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

namespace Rice {

class StreamingPanel {
public:
    ~StreamingPanel() { StopPreview(); }
    bool commandsOpen = false;
    bool cycleOpen = false;
    Streaming::CycleDefinition& CycleModel() noexcept { return cycle_; }
    const std::vector<std::string>& PreviewLog() const noexcept { return previewLog_; }
    void RegisterCycleFunction(std::string id, std::function<Streaming::ActionTask(const Streaming::CycleContext&)> function) {
        if (id.empty() || !function) throw std::invalid_argument("Native cycle function requires identity and callback");
        functions_[std::move(id)] = std::move(function);
    }
    void Draw(const std::filesystem::path& projectRoot, const std::string& sceneKey,
        Scene& scene, EntityID selectedObject, const std::function<void()>& beforeEdit = {}) {
        Guard([&] { Synchronize(projectRoot, sceneKey, scene); });
        if (commandsOpen) DrawCommands(scene, selectedObject, beforeEdit);
        if (cycleOpen) DrawCycle(scene);
        if (preview_ && autoPreview_) Guard([&] { AdvancePreview(ImGui::GetIO().DeltaTime); });
    }
    void SaveCycle() {
        if (root_.empty()) throw std::runtime_error("Open a project before saving streaming settings");
        cycle_.Save(root_ / "Settings" / "Streaming" / "cycle.json"); dirtyCycle_ = false;
    }
    bool HasUnsavedChanges()const{return dirtyCycle_||(hasDraft_&&originalDraft_!=draft_);}
    void SaveBeforeClose(Scene& scene){
        if(hasDraft_&&originalDraft_!=draft_){
            auto* object=scene.Find(draftObject_);
            auto* listener=object?object->GetComponent<YouTubeListener>():nullptr;
            if(!listener)throw std::runtime_error("The selected YouTube Listener is unavailable.");
            SaveDraft(scene,*listener,{});
        }
        if(dirtyCycle_)SaveCycle();
    }
    void CloseProject(){
        StopPreview();root_.clear();sceneKey_.clear();sceneInstance_=0;draftObject_=0;
        hasDraft_=dirtyCycle_=false;cycle_=Streaming::CycleDefinition{};error_.clear();
    }
    void SaveCommands(Scene& scene) {
        using Json = Serialization::Json;
        auto path = root_ / "Settings" / "Streaming" / "commands.json";
        auto previous = SettingsFile("commands.json");
        Json::Object scenes;
        if (std::filesystem::exists(previous)) scenes = ReadJson(previous).At("scenes").Fields();
        Json::Array rows;
        for (const auto& object : scene.GetObjects()) if (auto* listener = object->GetComponent<YouTubeListener>())
            rows.push_back(Json::Object{{"object", object->GetID()}, {"bindings", listener->commandBindings}});
        scenes[sceneKey_] = rows;
        Streaming::CycleDefinition::WriteFile(path, Json(Json::Object{{"schema", 1}, {"scenes", scenes}}).Dump());
    }
    // Local operator simulation calls the real receiver and scheduler and never
    // instantiates a YouTube client or sends an API request.
    void StartPreview(Scene& scene) {
        StopPreview();
        auto timeline = cycle_.Build(functions_);
        previewNow_ = 0;
        previewReceiver_ = std::make_shared<Streaming::GameCommandReceiver>("editor-preview", 1,
            scene.GetInstanceID(), "preview");
        previewReceiver_->SetActiveSlots({"preview"});
        previewReceiver_->authorize = [](const auto&, const auto&, const auto&) { return true; };
        for (const auto& object : scene.GetObjects()) if (auto* listener = object->GetComponent<YouTubeListener>()) previewReceiver_->Attach(*listener);
        preview_ = std::make_unique<Streaming::CycleScheduler>([this] { return previewNow_; });
        preview_->onBatchRequested = [this](auto ticket) { pendingPreview_ = ticket; AppendLog("Waiting for local batch"); };
        preview_->onCycle = [this](const auto& context) { AppendLog("OnCycle " + std::to_string(context.ticket.cycle)); };
        preview_->onResponse = [this](auto) { AppendLog("OnResponse"); };
        preview_->onDiagnostic = [this](const auto& message) { AppendLog(message); };
        preview_->SetDefinition(std::move(timeline), cycle_.Revision());
        preview_->Start(1, true, true);
        previewRevision_ = cycle_.Revision();
    }
    void StopPreview() { preview_.reset(); previewReceiver_.reset(); pendingPreview_.reset(); autoPreview_ = false; }
    void QueuePreviewCommand(std::string text) {
        if (!previewReceiver_) throw std::runtime_error("Start the local preview first");
        Streaming::RoutedYouTubeCommand command;
        command.text = std::move(text);
        command.context = {"editor-preview", 1, "preview", "preview", "local-preview",
            std::to_string(++previewMessage_), "local-operator"};
        command.inputEpoch = previewReceiver_->InputEpoch();
        command.publishedAtUnixMs = static_cast<std::int64_t>(previewNow_ * 1000) + 1;
        if (!previewReceiver_->Enqueue(std::move(command))) throw std::runtime_error("Preview input was not accepted");
    }
    void SubmitPreviewBatch(bool response = true) {
        if (!preview_ || !pendingPreview_) throw std::runtime_error("Preview is not waiting for a batch");
        Streaming::CycleBatch batch{*pendingPreview_, response, {}};
        auto receiver = previewReceiver_;
        for (auto& command : receiver->TakeBatch()) batch.commands.push_back([this, receiver, command = std::move(command)](const auto&) {
            auto result = receiver->Dispatch(command);
            AppendLog("Matched commands: " + std::to_string(result.items.size()));
            for (const auto& item : result.items) AppendLog(item.bindingId + ": " + DispatchLabel(item.status));
            for (const auto& diagnostic : result.diagnostics) AppendLog(diagnostic);
        });
        pendingPreview_.reset();
        if (response) preview_->NotifyResponse(batch.ticket.generation);
        preview_->SubmitBatch(std::move(batch));
    }
    void AdvancePreview(double seconds) {
        if (!preview_) return;
        if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid preview clock delta");
        if (previewRevision_ != cycle_.Revision()) {
            preview_->SetDefinition(cycle_.Build(functions_), cycle_.Revision());
            previewRevision_ = cycle_.Revision();
        }
        previewNow_ += seconds; preview_->Tick();
    }
private:
    using Json = Serialization::Json;
    using Config = Streaming::ListenerBindingConfig;
    Streaming::CycleDefinition cycle_;
    Streaming::CycleFunctions functions_;
    std::filesystem::path root_;
    std::string sceneKey_;
    std::uint64_t sceneInstance_ = 0;
    EntityID draftObject_ = 0;
    Config draft_;
    std::optional<Config> originalDraft_;
    bool hasDraft_ = false, dirtyCycle_ = false;
    std::string selectedAction_, selectedFunction_, testInput_ = "!회피 좌 3칸", commandResult_, error_;
    std::uint64_t nextId_ = 1;
    std::unique_ptr<Streaming::CycleScheduler> preview_;
    std::shared_ptr<Streaming::GameCommandReceiver> previewReceiver_;
    std::optional<Streaming::CycleTicket> pendingPreview_;
    double previewNow_ = 0;
    std::uint64_t previewRevision_ = 0, previewMessage_ = 0;
    bool autoPreview_ = false, simulateResponse_ = true;
    std::vector<std::string> previewLog_;

    template<class F> void Guard(F&& function) {
        try { function(); }
        catch (const std::exception& error) { error_ = error.what(); }
    }
    static bool EditText(const char* label, std::string& value) {
        std::vector<char> buffer(std::max<std::size_t>(1024, value.size() + 256), '\0');
        std::copy(value.begin(), value.end(), buffer.begin());
        if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
        value = buffer.data(); return true;
    }
    static Json ReadJson(const std::filesystem::path& path) {
        if (std::filesystem::file_size(path) > 4 * 1024 * 1024) throw std::runtime_error("Streaming settings exceed size limit");
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open streaming settings");
        return Json::Parse(std::string((std::istreambuf_iterator<char>(input)), {}));
    }
    std::filesystem::path SettingsFile(const char* name) const {
        auto current=root_/"Settings"/"Streaming"/name;
        return std::filesystem::exists(current)?current:root_/"Streaming"/name;
    }
    void Synchronize(const std::filesystem::path& root, const std::string& sceneKey, Scene& scene) {
        const bool projectChanged = root_ != root;
        const bool hadProject = !root_.empty();
        const bool sceneChanged = projectChanged || sceneInstance_ != scene.GetInstanceID() || sceneKey_ != sceneKey;
        if (!sceneChanged) return;
        StopPreview(); hasDraft_ = false; draftObject_ = 0;
        root_ = root; sceneKey_ = sceneKey; sceneInstance_ = scene.GetInstanceID();
        if (projectChanged) {
            auto path = SettingsFile("cycle.json");
            if (std::filesystem::exists(path)) cycle_.Load(path);
            else if (hadProject) cycle_ = Streaming::CycleDefinition{};
            dirtyCycle_ = false;
        }
        auto path = SettingsFile("commands.json");
        if (std::filesystem::exists(path)) {
            auto rootValue = ReadJson(path);
            if (rootValue.At("schema").Integer() != 1) throw std::runtime_error("Unsupported command settings schema");
            if (const auto* rows = rootValue.At("scenes").Find(sceneKey_)) {
                std::vector<std::pair<YouTubeListener*, std::string>> updates;
                for (const auto& row : rows->Items()) {
                    auto objectId = row.At("object").Integer();
                    if (objectId <= 0) throw std::runtime_error("Invalid saved listener object ID");
                    auto configuration = row.At("bindings").String();
                    YouTubeListener::DecodeBindings(configuration);
                    if (auto* object = scene.Find(static_cast<EntityID>(objectId)))
                        if (auto* listener = object->GetComponent<YouTubeListener>()) updates.emplace_back(listener, std::move(configuration));
                }
                for (auto& [listener, configuration] : updates) listener->commandBindings = std::move(configuration);
            }
        }
    }
    void SaveDraft(Scene& scene,YouTubeListener& listener,const std::function<void()>& beforeEdit){
        const auto& catalog=listener.RegisteredBindings();
        auto prototype=std::find_if(catalog.begin(),catalog.end(),[&](const auto& binding){return binding.pattern.Function().functionId==draft_.functionId;});
        if(prototype==catalog.end())throw std::runtime_error("Select a registered YouTube command function.");
        prototype->pattern.WithPattern(draft_.pattern);
        auto overrides=YouTubeListener::DecodeBindings(listener.commandBindings);
        auto existing=std::find_if(overrides.begin(),overrides.end(),[&](const auto& row){return row.bindingId==draft_.bindingId;});
        if(existing==overrides.end())overrides.push_back(draft_);else *existing=draft_;
        auto encoded=YouTubeListener::EncodeBindings(overrides);
        if(beforeEdit)beforeEdit();
        listener.commandBindings.swap(encoded);
        try{SaveCommands(scene);}catch(...){listener.commandBindings.swap(encoded);throw;}
        originalDraft_=draft_;
    }
    static std::vector<Config> Rows(const YouTubeListener& listener) {
        auto overrides = YouTubeListener::DecodeBindings(listener.commandBindings);
        std::vector<Config> result;
        const auto& bindings = listener.RegisteredBindings();
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            const auto& binding = bindings[index];
            auto found = std::find_if(overrides.begin(), overrides.end(), [&](const auto& row) { return row.bindingId == binding.bindingId; });
            if (found != overrides.end()) result.push_back(*found);
            else result.push_back({binding.bindingId, binding.pattern.Function().functionId, binding.pattern.Text(),
                binding.enabled, index, binding.permission, binding.allowedChannelIds});
        }
        for (const auto& row : overrides) if (std::none_of(result.begin(), result.end(), [&](const auto& existing) { return existing.bindingId == row.bindingId; })) result.push_back(row);
        std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.order < right.order; });
        return result;
    }
    static const char* TypeLabel(Streaming::CommandValueType type) {
        switch (type) {
            case Streaming::CommandValueType::Int32: return "int32";
            case Streaming::CommandValueType::Int64: return "int64";
            case Streaming::CommandValueType::UInt32: return "uint32";
            case Streaming::CommandValueType::UInt64: return "uint64";
            case Streaming::CommandValueType::Float: return "float";
            case Streaming::CommandValueType::Double: return "double";
            case Streaming::CommandValueType::Bool: return "bool";
            default: return "string";
        }
    }
    static const char* DispatchLabel(Streaming::CommandDispatchStatus status) {
        switch (status) {
            case Streaming::CommandDispatchStatus::Executed: return "executed";
            case Streaming::CommandDispatchStatus::Denied: return "denied";
            case Streaming::CommandDispatchStatus::Unavailable: return "unavailable";
            case Streaming::CommandDispatchStatus::InvalidArguments: return "invalid arguments";
            default: return "failed";
        }
    }
    void DrawCommands(Scene& scene, EntityID selected, const std::function<void()>& beforeEdit) {
        ImGui::SetNextWindowSize({900, 570}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Streaming - YouTube Commands", &commandsOpen)) {
            auto* object = scene.Find(selected);
            auto* listener = object ? object->GetComponent<YouTubeListener>() : nullptr;
            if (!listener) ImGui::TextWrapped("Select an object with a YouTube Listener component.");
            else Guard([&] {
                auto rows = Rows(*listener);
                if (draftObject_ != selected) { draftObject_ = selected; hasDraft_ = false; }
                ImGui::Text("Object: %s", object->name.c_str());
                ImGui::BeginChild("Commands", {300, -150}, ImGuiChildFlags_Borders);
                for (const auto& row : rows) {
                    std::string label = (row.enabled ? "" : "[off] ") + row.pattern + "##" + row.bindingId;
                    if (ImGui::Selectable(label.c_str(), hasDraft_ && draft_.bindingId == row.bindingId)) { draft_ = row;originalDraft_=row; hasDraft_ = true; }
                }
                ImGui::BeginDisabled(listener->RegisteredBindings().empty());
                if (ImGui::Button("Add command")) {
                    const auto& prototype = listener->RegisteredBindings().front();
                    draft_ = {"manual-" + std::to_string(nextId_++), prototype.pattern.Function().functionId,
                        prototype.pattern.Text(), true, rows.size(), "LocalAdmin", {}};
                    originalDraft_.reset();hasDraft_ = true;
                }
                ImGui::EndDisabled();
                ImGui::EndChild(); ImGui::SameLine();
                ImGui::BeginChild("Native function", {0, -150}, ImGuiChildFlags_Borders);
                if (listener->RegisteredBindings().empty()) ImGui::TextWrapped("No native functions are registered on this Listener.");
                if (hasDraft_) {
                    ImGui::Text("Binding: %s", draft_.bindingId.c_str());
                    EditText("Pattern", draft_.pattern);
                    if (ImGui::BeginCombo("Function", draft_.functionId.c_str())) {
                        std::unordered_set<std::string> shown;
                        for (const auto& binding : listener->RegisteredBindings()) {
                            const auto& id = binding.pattern.Function().functionId;
                            if (shown.insert(id).second && ImGui::Selectable(id.c_str(), id == draft_.functionId)) draft_.functionId = id;
                        }
                        ImGui::EndCombo();
                    }
                    const auto& catalog = listener->RegisteredBindings();
                    auto prototype = std::find_if(catalog.begin(), catalog.end(), [&](const auto& binding) { return binding.pattern.Function().functionId == draft_.functionId; });
                    if (prototype != catalog.end()) for (const auto& argument : prototype->pattern.Function().parameters)
                        ImGui::Text("%s: %s%s", argument.name.c_str(), TypeLabel(argument.type), argument.defaultValue ? " (registered default)" : "");
                    ImGui::Checkbox("Enabled", &draft_.enabled);
                    int order = static_cast<int>(draft_.order);
                    if (ImGui::InputInt("Order", &order)) draft_.order = static_cast<std::size_t>(std::clamp(order, 0, 1000000));
                    if (ImGui::BeginCombo("Permission", draft_.permission.c_str())) {
                        for (const char* permission : {"LocalAdmin", "Everyone", "Owner", "OwnerOrModerator", "ChannelAllowlist"})
                            if (ImGui::Selectable(permission, draft_.permission == permission)) draft_.permission = permission;
                        ImGui::EndCombo();
                    }
                    if (draft_.permission == "ChannelAllowlist") {
                        std::string channels;
                        for (const auto& channel : draft_.allowedChannelIds) { if (!channels.empty()) channels += ','; channels += channel; }
                        if (EditText("Channel IDs (comma separated)", channels)) {
                            draft_.allowedChannelIds.clear(); std::istringstream input(channels); std::string channel;
                            while (std::getline(input, channel, ',')) if (!channel.empty()) draft_.allowedChannelIds.push_back(channel);
                        }
                    }
                    ImGui::BeginDisabled(prototype == catalog.end());
                    if (ImGui::Button("Apply and save")) Guard([&] {SaveDraft(scene,*listener,beforeEdit);});
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Restore script binding")) Guard([&] {
                        auto overrides = YouTubeListener::DecodeBindings(listener->commandBindings);
                        std::erase_if(overrides, [&](const auto& row) { return row.bindingId == draft_.bindingId; });
                        if (beforeEdit) beforeEdit(); listener->SetBindingConfiguration(overrides); SaveCommands(scene); hasDraft_ = false;
                    });
                }
                ImGui::EndChild();
                EditText("Test input", testInput_);
                if (ImGui::Button("Match only")) {
                    commandResult_.clear();
                    for (const auto& binding : listener->BuildBindings()) if (binding.enabled) {
                        if (binding.pattern.Matches(testInput_)) commandResult_ += binding.bindingId + ": pattern match; ";
                    }
                    if (commandResult_.empty()) commandResult_ = "No matching commands";
                }
                ImGui::SameLine();
                if (ImGui::Button("Queue local test")) { if (!preview_) StartPreview(scene); QueuePreviewCommand(testInput_); cycleOpen = true; }
                ImGui::TextWrapped("%s", commandResult_.c_str());
                ImGui::TextWrapped("Match only checks patterns. Local tests execute functions in the current scene and can change game data.");
                ImGui::TextDisabled("No YouTube requests. Player/Controller transport is a separate runtime connection.");
            });
            if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        }
        ImGui::End();
    }
    void DrawCycle(Scene& scene) {
        ImGui::SetNextWindowSize({1000, 700}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Streaming - Cycle Timeline", &cycleOpen)) {
            Guard([&] {
                float cycleTime = static_cast<float>(cycle_.CycleTime());
                if (ImGui::DragFloat("Cycle length (seconds)", &cycleTime, .1f, 1.f, 3600.f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) { cycle_.SetCycleTime(cycleTime); dirtyCycle_ = true; }
                ImGui::SameLine(); if (ImGui::Button("Use script length")) { cycle_.SetCycleTime({}); dirtyCycle_ = true; }
                if (ImGui::Button("Save timeline")) SaveCycle();
                ImGui::SameLine(); ImGui::TextUnformatted(dirtyCycle_ ? "Unsaved inspector changes" : "Saved / script defaults");
                if (ImGui::BeginCombo("Native action", selectedFunction_.c_str())) {
                    std::vector<std::string> names; for (const auto& [name, unused] : functions_) { (void)unused; names.push_back(name); }
                    std::sort(names.begin(), names.end());
                    for (const auto& name : names) if (ImGui::Selectable(name.c_str(), name == selectedFunction_)) selectedFunction_ = name;
                    ImGui::EndCombo();
                }
                ImGui::SameLine(); ImGui::BeginDisabled(selectedFunction_.empty());
                if (ImGui::Button("Add action")) Guard([&] {
                    Streaming::CycleActionDefinition action; action.actionId = "action-" + std::to_string(nextId_++);
                    action.functionId = selectedFunction_; action.name = selectedFunction_; action.order = static_cast<int>(cycle_.EffectiveActions().size());
                    cycle_.Add(action); selectedAction_ = action.actionId; dirtyCycle_ = true;
                });
                ImGui::EndDisabled();
                auto actions = cycle_.EffectiveActions();
                const float span = std::max(1.f, cycleTime);
                ImGui::BeginChild("Timeline", {0, 240}, ImGuiChildFlags_Borders);
                ImGui::TextUnformatted("0  OnCycle   |   cycleStart + 0 s   (fixed)");
                for (std::size_t index = 0; index < actions.size(); ++index) {
                    const auto& action = actions[index];
                    ImGui::PushID(action.actionId.c_str());
                    auto label = std::to_string(index + 1) + "  " + (action.name.empty() ? action.actionId : action.name);
                    if (ImGui::Selectable(label.c_str(), selectedAction_ == action.actionId, 0, {220, 26})) selectedAction_ = action.actionId;
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("RiceCycleAction", action.actionId.c_str(), action.actionId.size() + 1);
                        ImGui::TextUnformatted(label.c_str()); ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const auto* payload = ImGui::AcceptDragDropPayload("RiceCycleAction")) Guard([&] {
                            if (payload->DataSize <= 1) throw std::invalid_argument("Invalid action drag payload");
                            cycle_.Move(std::string(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize - 1)), index); dirtyCycle_ = true;
                        });
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                    auto origin = ImGui::GetCursorScreenPos();
                    const float width = std::max(100.f, ImGui::GetContentRegionAvail().x);
                    ImGui::InvisibleButton("Time handle", {width, 26});
                    auto* draw = ImGui::GetWindowDrawList();
                    for (int line = 0; line <= 10; ++line) {
                        const float x = origin.x + width * static_cast<float>(line) / 10.f;
                        draw->AddLine({x, origin.y}, {x, origin.y + 26}, IM_COL32(80, 88, 100, 150));
                    }
                    const float x = origin.x + std::clamp(action.timeFrom / span, 0.f, 1.f) * width;
                    draw->AddCircleFilled({x, origin.y + 13}, 6, action.isCycleBlocked ? IM_COL32(255, 183, 80, 255) : IM_COL32(84, 175, 250, 255));
                    const char* from = action.reference == Streaming::CycleAction::from::prev ? "prev complete" : action.reference == Streaming::CycleAction::from::response ? "API response" : "cycleStart";
                    const auto caption = std::string(from) + " + " + std::to_string(action.timeFrom) + " s";
                    draw->AddText({origin.x + 10, origin.y + 5}, IM_COL32(225, 225, 225, 255), caption.c_str());
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) Guard([&] {
                        Streaming::CycleActionOverride patch; patch.timeFrom = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / width * span, 0.f, span);
                        cycle_.Patch(action.actionId, patch); selectedAction_ = action.actionId; dirtyCycle_ = true;
                    });
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag the marker to change delay. Previous completion and response anchors are dynamic; task duration is unknown.");
                    ImGui::PopID();
                }
                ImGui::EndChild();
                auto selected = std::find_if(actions.begin(), actions.end(), [&](const auto& action) { return action.actionId == selectedAction_; });
                if (selected != actions.end()) DrawActionDetails(*selected, static_cast<std::size_t>(std::distance(actions.begin(), selected)), actions.size());
                for (const auto& orphan : cycle_.OrphanedOverrides()) ImGui::TextWrapped("Missing script action: %s (override retained, not executed)", orphan.c_str());
                ImGui::SeparatorText("Local cycle test");
                ImGui::TextWrapped("Start/submit runs native actions and commands in the current scene and can change game data.");
                if (ImGui::Button(preview_ ? "Restart local test" : "Start local test")) StartPreview(scene);
                ImGui::SameLine(); ImGui::BeginDisabled(!preview_);
                if (ImGui::Button("Stop test")) StopPreview();
                ImGui::SameLine(); if (ImGui::Button("Advance 1 second")) Guard([&] { AdvancePreview(1); });
                ImGui::SameLine(); ImGui::Checkbox("Run clock", &autoPreview_);
                ImGui::EndDisabled();
                ImGui::Checkbox("Simulate API response", &simulateResponse_);
                ImGui::SameLine(); ImGui::BeginDisabled(!pendingPreview_);
                if (ImGui::Button("Submit local batch")) Guard([&] { SubmitPreviewBatch(simulateResponse_); });
                ImGui::EndDisabled();
                if (preview_) ImGui::Text("Remaining: %.3f s | Applied revision: %llu", preview_->RemainingSeconds(), static_cast<unsigned long long>(preview_->AppliedRevision()));
                ImGui::BeginChild("Local test log", {0, 100}, ImGuiChildFlags_Borders);
                for (const auto& line : previewLog_) ImGui::TextUnformatted(line.c_str());
                ImGui::EndChild();
            });
            if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
        }
        ImGui::End();
    }
    void DrawActionDetails(Streaming::CycleActionDefinition action, std::size_t index, std::size_t count) {
        Streaming::CycleActionOverride patch;
        bool changed = false;
        ImGui::Text("Action: %s%s", action.actionId.c_str(), cycle_.HasOverride(action.actionId) ? " (inspector override)" : "");
        if (EditText("Action name", action.name)) { patch.name = action.name; changed = true; }
        int reference = static_cast<int>(action.reference);
        if (ImGui::Combo("From", &reference, "Previous completion\0Cycle start\0API response\0")) { patch.reference = static_cast<Streaming::CycleAction::from>(reference); changed = true; }
        if (ImGui::DragFloat("Delay seconds", &action.timeFrom, .05f, 0.f, 3600.f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) { patch.timeFrom = action.timeFrom; changed = true; }
        if (ImGui::Checkbox("Block cycle during execution", &action.isCycleBlocked)) { patch.isCycleBlocked = action.isCycleBlocked; changed = true; }
        ImGui::SameLine(); if (ImGui::Checkbox("Stop on cycle end", &action.isStoppedByCycle)) { patch.isStoppedByCycle = action.isStoppedByCycle; changed = true; }
        ImGui::SameLine(); if (ImGui::Checkbox("Run without response", &action.isActionOnNoResponse)) { patch.isActionOnNoResponse = action.isActionOnNoResponse; changed = true; }
        if (changed) { cycle_.Patch(action.actionId, patch); dirtyCycle_ = true; }
        ImGui::BeginDisabled(index == 0); if (ImGui::Button("Move up")) Guard([&] { cycle_.Move(action.actionId, index - 1); dirtyCycle_ = true; }); ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::BeginDisabled(index + 1 >= count); if (ImGui::Button("Move down")) Guard([&] { cycle_.Move(action.actionId, index + 1); dirtyCycle_ = true; }); ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button("Remove action")) { cycle_.Remove(action.actionId); selectedAction_.clear(); dirtyCycle_ = true; }
        ImGui::SameLine(); if (ImGui::Button("Restore script values")) { cycle_.ResetOverride(action.actionId); dirtyCycle_ = true; }
    }
    void AppendLog(std::string line) {
        if (previewLog_.size() >= 100) previewLog_.erase(previewLog_.begin());
        previewLog_.push_back(std::move(line));
    }
};
} // namespace Rice
