#include "CycleDefinition.h"
#include "Serialization/Json.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Rice::Streaming {
namespace {
using Json = Rice::Serialization::Json;
std::int64_t Microseconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 9000000000000.0)
        throw std::invalid_argument("Cycle time must be finite, non-negative and representable");
    return static_cast<std::int64_t>(std::round(seconds * 1000000.0));
}
double Seconds(const Json& value) {
    auto number = value.Integer();
    if (number < 0) throw std::invalid_argument("Negative cycle time");
    return static_cast<double>(number) / 1000000.0;
}
void ValidateLength(double seconds) {
    if (!std::isfinite(seconds) || seconds < 1 || seconds > 3600)
        throw std::invalid_argument("Cycle length must be within 1..3600 seconds");
}
CycleAction::from Reference(std::int64_t value) {
    if (value < 0 || value > 2) throw std::invalid_argument("Invalid action reference");
    return static_cast<CycleAction::from>(value);
}
int Order(std::int64_t value) {
    if (value < 0 || value > 1000000) throw std::invalid_argument("Invalid action order");
    return static_cast<int>(value);
}
void Validate(const CycleActionDefinition& action) {
    if (action.actionId.empty() || action.functionId.empty() || action.actionId == "__rice_on_cycle__")
        throw std::invalid_argument("Action requires a non-reserved ID and native function ID");
    if (action.actionId.size() > 512 || action.functionId.size() > 512 || action.name.size() > 1024)
        throw std::invalid_argument("Action metadata is too long");
    Microseconds(action.timeFrom); Reference(static_cast<int>(action.reference)); Order(action.order);
}
Json Encode(const CycleActionDefinition& action) {
    return Json::Object{
        {"id", action.actionId}, {"function", action.functionId}, {"name", action.name},
        {"from", static_cast<int>(action.reference)}, {"delay_us", Microseconds(action.timeFrom)},
        {"blocked", action.isCycleBlocked}, {"stop_on_cycle", action.isStoppedByCycle},
        {"on_no_response", action.isActionOnNoResponse}, {"order", action.order}
    };
}
CycleActionDefinition Decode(const Json& value) {
    CycleActionDefinition action;
    action.actionId = value.At("id").String(); action.functionId = value.At("function").String();
    action.name = value.At("name").String(); action.reference = Reference(value.At("from").Integer());
    action.timeFrom = static_cast<float>(Seconds(value.At("delay_us")));
    action.isCycleBlocked = value.At("blocked").Boolean(); action.isStoppedByCycle = value.At("stop_on_cycle").Boolean();
    action.isActionOnNoResponse = value.At("on_no_response").Boolean(); action.order = Order(value.At("order").Integer());
    Validate(action);
    return action;
}
void Merge(CycleActionOverride& target, const CycleActionOverride& patch) {
    if (patch.functionId) target.functionId = patch.functionId;
    if (patch.name) target.name = patch.name;
    if (patch.reference) target.reference = patch.reference;
    if (patch.timeFrom) target.timeFrom = patch.timeFrom;
    if (patch.isCycleBlocked) target.isCycleBlocked = patch.isCycleBlocked;
    if (patch.isStoppedByCycle) target.isStoppedByCycle = patch.isStoppedByCycle;
    if (patch.isActionOnNoResponse) target.isActionOnNoResponse = patch.isActionOnNoResponse;
    if (patch.order) target.order = patch.order;
    target.removed = patch.removed;
}
void Apply(CycleActionDefinition& action, const CycleActionOverride& patch) {
    if (patch.functionId) action.functionId = *patch.functionId;
    if (patch.name) action.name = *patch.name;
    if (patch.reference) action.reference = *patch.reference;
    if (patch.timeFrom) action.timeFrom = *patch.timeFrom;
    if (patch.isCycleBlocked) action.isCycleBlocked = *patch.isCycleBlocked;
    if (patch.isStoppedByCycle) action.isStoppedByCycle = *patch.isStoppedByCycle;
    if (patch.isActionOnNoResponse) action.isActionOnNoResponse = *patch.isActionOnNoResponse;
    if (patch.order) action.order = *patch.order;
}
}
void CycleDefinition::SetScriptBaseline(double cycleTime, std::vector<CycleActionDefinition> actions) {
    ValidateLength(cycleTime);
    if (actions.size() > 4096) throw std::invalid_argument("Too many cycle actions");
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        auto& action = actions[index]; action.order = static_cast<int>(index);
        Validate(action);
        if (!ids.insert(action.actionId).second) throw std::invalid_argument("Duplicate script ActionId");
        if (std::any_of(added_.begin(), added_.end(), [&](const auto& added) { return added.actionId == action.actionId; }))
            throw std::invalid_argument("Script ActionId collides with an inspector-added action");
    }
    scriptCycleTime_ = cycleTime; scriptActions_ = std::move(actions); ++revision_;
}
void CycleDefinition::SetCycleTime(std::optional<double> seconds) {
    if (seconds) ValidateLength(*seconds);
    cycleTimeOverride_ = seconds; ++revision_;
}
void CycleDefinition::Add(CycleActionDefinition action) {
    Validate(action);
    auto effective = EffectiveActions();
    if (effective.size() >= 4096) throw std::invalid_argument("Too many cycle actions");
    auto exists = [&](const auto& item) { return item.actionId == action.actionId; };
    if (std::any_of(scriptActions_.begin(), scriptActions_.end(), exists) || std::any_of(added_.begin(), added_.end(), exists))
        throw std::invalid_argument("Duplicate ActionId");
    added_.push_back(std::move(action)); ++revision_;
}
void CycleDefinition::Patch(const std::string& actionId, const CycleActionOverride& patch) {
    auto effective = EffectiveActions();
    auto found = std::find_if(effective.begin(), effective.end(), [&](const auto& item) { return item.actionId == actionId; });
    if (found == effective.end()) throw std::invalid_argument("Unknown active ActionId");
    Apply(*found, patch); Validate(*found);
    Merge(overrides_[actionId], patch); ++revision_;
}
void CycleDefinition::Remove(const std::string& actionId) {
    auto found = std::find_if(added_.begin(), added_.end(), [&](const auto& item) { return item.actionId == actionId; });
    if (found != added_.end()) { added_.erase(found); overrides_.erase(actionId); ++revision_; return; }
    CycleActionOverride patch; patch.removed = true; Patch(actionId, patch);
}
void CycleDefinition::Move(const std::string& actionId, std::size_t index) {
    auto effective = EffectiveActions();
    if (index >= effective.size()) throw std::out_of_range("Action move index");
    auto found = std::find_if(effective.begin(), effective.end(), [&](const auto& item) { return item.actionId == actionId; });
    if (found == effective.end()) throw std::invalid_argument("Unknown ActionId");
    auto moving = *found; effective.erase(found); effective.insert(effective.begin() + static_cast<std::ptrdiff_t>(index), moving);
    for (std::size_t current = 0; current < effective.size(); ++current)
        overrides_[effective[current].actionId].order = static_cast<int>(current);
    ++revision_;
}
void CycleDefinition::ResetOverride(const std::string& actionId) { overrides_.erase(actionId); ++revision_; }
bool CycleDefinition::HasOverride(const std::string& actionId) const { return overrides_.contains(actionId); }
std::vector<CycleActionDefinition> CycleDefinition::EffectiveActions() const {
    auto effective = scriptActions_;
    effective.insert(effective.end(), added_.begin(), added_.end());
    for (auto& action : effective) {
        auto patch = overrides_.find(action.actionId);
        if (patch != overrides_.end()) Apply(action, patch->second);
    }
    std::erase_if(effective, [&](const auto& action) {
        auto patch = overrides_.find(action.actionId);
        return patch != overrides_.end() && patch->second.removed;
    });
    std::stable_sort(effective.begin(), effective.end(), [](const auto& left, const auto& right) { return left.order < right.order; });
    return effective;
}
std::vector<std::string> CycleDefinition::OrphanedOverrides() const {
    std::vector<std::string> result;
    for (const auto& [id, unused] : overrides_) {
        (void)unused;
        const auto match = [&](const auto& action) { return action.actionId == id; };
        if (std::none_of(scriptActions_.begin(), scriptActions_.end(), match) && std::none_of(added_.begin(), added_.end(), match)) result.push_back(id);
    }
    return result;
}
CycleActionList CycleDefinition::Build(const CycleFunctions& functions) const {
    CycleActionList result; result.cycleTime = CycleTime();
    for (const auto& spec : EffectiveActions()) {
        auto function = functions.find(spec.functionId);
        if (function == functions.end() || !function->second) throw std::invalid_argument("Unregistered native cycle function: " + spec.functionId);
        CycleAction action;
        action.ActionId = spec.actionId; action.invoke = function->second; action.reference = spec.reference;
        action.timeFrom = spec.timeFrom; action.isCycleBlocked = spec.isCycleBlocked;
        action.isStoppedByCycle = spec.isStoppedByCycle; action.isActionOnNoResponse = spec.isActionOnNoResponse;
        result.Add(std::move(action));
    }
    result.Validate();
    return result;
}
std::string CycleDefinition::Serialize() const {
    Json::Array baseline, added;
    for (const auto& item : scriptActions_) baseline.push_back(Encode(item));
    for (const auto& item : added_) added.push_back(Encode(item));
    Json::Object overrides;
    for (const auto& [id, patch] : overrides_) {
        Json::Object value{{"removed", patch.removed}};
        if (patch.functionId) value["function"] = *patch.functionId;
        if (patch.name) value["name"] = *patch.name;
        if (patch.reference) value["from"] = static_cast<int>(*patch.reference);
        if (patch.timeFrom) value["delay_us"] = Microseconds(*patch.timeFrom);
        if (patch.isCycleBlocked) value["blocked"] = *patch.isCycleBlocked;
        if (patch.isStoppedByCycle) value["stop_on_cycle"] = *patch.isStoppedByCycle;
        if (patch.isActionOnNoResponse) value["on_no_response"] = *patch.isActionOnNoResponse;
        if (patch.order) value["order"] = *patch.order;
        overrides[id] = value;
    }
    Json::Object root{{"schema", 1}, {"revision", revision_}, {"script_cycle_us", Microseconds(scriptCycleTime_)},
        {"script_actions", baseline}, {"added_actions", added}, {"overrides", overrides}};
    if (cycleTimeOverride_) root["cycle_override_us"] = Microseconds(*cycleTimeOverride_);
    return Json(root).Dump();
}
void CycleDefinition::Deserialize(std::string_view json) {
    auto root = Json::Parse(json);
    if (root.At("schema").Integer() != 1) throw std::invalid_argument("Unsupported cycle schema");
    CycleDefinition next;
    const auto revision = root.At("revision").Integer();
    if (revision < 1) throw std::invalid_argument("Invalid cycle revision");
    next.revision_ = static_cast<std::uint64_t>(revision);
    next.scriptCycleTime_ = Seconds(root.At("script_cycle_us")); ValidateLength(next.scriptCycleTime_);
    if (auto time = root.Find("cycle_override_us")) { next.cycleTimeOverride_ = Seconds(*time); ValidateLength(*next.cycleTimeOverride_); }
    std::unordered_set<std::string> ids;
    auto read = [&](const Json& array, auto& target) {
        if (array.Items().size() > 4096) throw std::invalid_argument("Too many stored cycle actions");
        for (const auto& value : array.Items()) {
            auto action = Decode(value);
            if (!ids.insert(action.actionId).second) throw std::invalid_argument("Duplicate stored ActionId");
            target.push_back(std::move(action));
        }
    };
    read(root.At("script_actions"), next.scriptActions_); read(root.At("added_actions"), next.added_);
    if (next.scriptActions_.size() + next.added_.size() > 4096 || root.At("overrides").Fields().size() > 8192)
        throw std::invalid_argument("Cycle definition exceeds action limits");
    for (const auto& [id, value] : root.At("overrides").Fields()) {
        if (id.empty() || id == "__rice_on_cycle__" || id.size() > 512) throw std::invalid_argument("Invalid override ActionId");
        CycleActionOverride patch;
        patch.removed = value.At("removed").Boolean();
        if (auto item = value.Find("function")) { patch.functionId = item->String(); if (patch.functionId->empty() || patch.functionId->size() > 512) throw std::invalid_argument("Invalid native function ID"); }
        if (auto item = value.Find("name")) { patch.name = item->String(); if (patch.name->size() > 1024) throw std::invalid_argument("Invalid action name"); }
        if (auto item = value.Find("from")) patch.reference = Reference(item->Integer());
        if (auto item = value.Find("delay_us")) patch.timeFrom = static_cast<float>(Seconds(*item));
        if (auto item = value.Find("blocked")) patch.isCycleBlocked = item->Boolean();
        if (auto item = value.Find("stop_on_cycle")) patch.isStoppedByCycle = item->Boolean();
        if (auto item = value.Find("on_no_response")) patch.isActionOnNoResponse = item->Boolean();
        if (auto item = value.Find("order")) patch.order = Order(item->Integer());
        next.overrides_.emplace(id, std::move(patch));
    }
    for (const auto& action : next.EffectiveActions()) Validate(action);
    *this = std::move(next);
}
void CycleDefinition::WriteFile(const std::filesystem::path& path, std::string_view text) {
    if (text.size() > 4 * 1024 * 1024) throw std::invalid_argument("Streaming settings exceed size limit");
    std::filesystem::create_directories(path.parent_path());
    static std::atomic<std::uint64_t> serial{1};
    const auto process =
#ifdef _WIN32
        _getpid();
#else
        getpid();
#endif
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = path;
    temporary += ".tmp." + std::to_string(process) + "." + std::to_string(stamp) + "." + std::to_string(serial++);
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size())); output.flush();
        if (!output) throw std::runtime_error("Cannot write streaming settings");
        output.close();
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code error; std::filesystem::remove(temporary, error); throw;
    }
}
void CycleDefinition::Save(const std::filesystem::path& path) const { WriteFile(path, Serialize()); }
void CycleDefinition::Load(const std::filesystem::path& path) {
    constexpr std::size_t Limit = 4 * 1024 * 1024;
    if (std::filesystem::file_size(path) > Limit) throw std::runtime_error("Streaming cycle settings exceed size limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open streaming cycle settings");
    std::string text;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        const auto count = static_cast<std::size_t>(input.gcount());
        if (text.size() + count > Limit) throw std::runtime_error("Streaming cycle settings grew beyond size limit");
        text.append(buffer, count);
    }
    if (!input.eof()) throw std::runtime_error("Cannot read streaming cycle settings");
    Deserialize(text);
}
} // namespace Rice::Streaming
