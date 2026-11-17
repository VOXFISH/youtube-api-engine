#pragma once

#include "CycleScheduler.h"

#include <filesystem>
#include <map>
#include <unordered_map>

namespace Rice::Streaming {

struct CycleActionDefinition {
    std::string actionId;
    std::string functionId;
    std::string name;
    CycleAction::from reference = CycleAction::from::cycleStart;
    float timeFrom = 0;
    bool isCycleBlocked = false;
    bool isStoppedByCycle = false;
    bool isActionOnNoResponse = true;
    int order = 0;
};
struct CycleActionOverride {
    std::optional<std::string> functionId;
    std::optional<std::string> name;
    std::optional<CycleAction::from> reference;
    std::optional<float> timeFrom;
    std::optional<bool> isCycleBlocked;
    std::optional<bool> isStoppedByCycle;
    std::optional<bool> isActionOnNoResponse;
    std::optional<int> order;
    bool removed = false;
};
using CycleFunctions = std::unordered_map<std::string, std::function<ActionTask(const CycleContext&)>>;

// Serializable definitions and field-level inspector overrides. No function
// addresses are persisted; native callbacks are supplied by the game at build.
class CycleDefinition {
public:
    void SetScriptBaseline(double cycleTime, std::vector<CycleActionDefinition> actions);
    void SetCycleTime(std::optional<double> seconds);
    double CycleTime() const noexcept { return cycleTimeOverride_.value_or(scriptCycleTime_); }
    bool HasCycleTimeOverride() const noexcept { return cycleTimeOverride_.has_value(); }
    std::uint64_t Revision() const noexcept { return revision_; }
    void Add(CycleActionDefinition action);
    void Patch(const std::string& actionId, const CycleActionOverride& patch);
    void Remove(const std::string& actionId);
    void Move(const std::string& actionId, std::size_t index);
    void ResetOverride(const std::string& actionId);
    bool HasOverride(const std::string& actionId) const;
    std::vector<CycleActionDefinition> EffectiveActions() const;
    std::vector<std::string> OrphanedOverrides() const;
    CycleActionList Build(const CycleFunctions& functions) const;
    std::string Serialize() const;
    void Deserialize(std::string_view json);
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
    static void WriteFile(const std::filesystem::path& path, std::string_view text);
private:
    double scriptCycleTime_ = 25;
    std::vector<CycleActionDefinition> scriptActions_;
    std::optional<double> cycleTimeOverride_;
    std::vector<CycleActionDefinition> added_;
    std::map<std::string, CycleActionOverride> overrides_;
    std::uint64_t revision_ = 1;
};

} // namespace Rice::Streaming
