#pragma once

#include "Scene.h"
#include "Rice/AssetID.h"
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace Rice {
namespace Scripting { struct Value; }

class ScriptExecution {
public:
    virtual ~ScriptExecution() = default;
    virtual void Tick(float seconds) = 0;
    virtual bool Read(const char* name, Scripting::Value& value, uint64_t symbol = 0) = 0;
    virtual bool TriggerBlocking(uint64_t) const { return false; }
};

// The three creation presets retain their legacy registry identities. This
// keeps all three independent attachments in old scenes, prefabs and history.
struct ScriptComponentBase : MonoBehaviour {
    std::string scriptFile;
    std::string childBindings;
    std::string childReads;
    std::string dataObjectKey = NewAssetID();

    std::map<std::string, EntityID> Bindings() const {
        std::map<std::string, EntityID> bindings;
        std::istringstream input(childBindings);
        std::string key;
        EntityID id;
        while (input >> std::quoted(key) >> id) bindings[key] = id;
        return bindings;
    }
    EntityID BoundChild(std::string_view key) const {
        auto bindings = Bindings();
        auto found = bindings.find(std::string(key));
        return found == bindings.end() ? 0 : found->second;
    }
    void SetBindings(const std::map<std::string, EntityID>& values) {
        std::ostringstream out;
        for (const auto& [key, id] : values)
            if (id) out << std::quoted(key) << ' ' << id << '\n';
        childBindings = out.str();
    }
    void BindChild(std::string_view key, EntityID id) {
        auto values = Bindings();
        values[std::string(key)] = id;
        SetBindings(values);
    }
    ScriptExecution* Execution() {
        if (scriptFile.empty()) return nullptr;
        auto& create = GetGameObject().GetScene().createScriptExecution;
        if (!attempted && create) {
            attempted = true;
            instance = create(*this);
        }
        return instance.get();
    }
    void Update(float dt) override { if (auto* script = Execution()) script->Tick(dt); }
    bool TriggerBlocking(uint64_t row) const { return instance&&instance->TriggerBlocking(row); }
    void OnPoolRelease() override { ResetExecution(); }
    void OnDestroy() override { ResetExecution(); }
    void ResetExecution() { instance.reset(); attempted = false; }
private:
    std::unique_ptr<ScriptExecution> instance;
    bool attempted = false;
};

template<class MapId>
void RemapScriptBindings(GameObject& object, MapId mapId) {
    object.EachComponent([&](Component& c) {
        auto* script = dynamic_cast<ScriptComponentBase*>(&c);
        if (!script) return;
        script->dataObjectKey = NewAssetID();
        if (script->childBindings.empty()) return;
        auto values = script->Bindings();
        for (auto& [key, id] : values) id = mapId(id);
        script->SetBindings(values);
    });
}
}
