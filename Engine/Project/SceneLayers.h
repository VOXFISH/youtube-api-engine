#pragma once
#include "Scene/Scene.h"
#include <unordered_set>

namespace Rice::SceneLayers {
inline EntityID NextLayerID(const Scene& scene) {
    EntityID highest = 0;
    for (const auto& object : scene.GetObjects()) highest = std::max(highest, object->GetID());
    if (highest < UINT64_MAX - 4) return highest + 1;
    EntityID id = 1;
    while (scene.Find(id)) ++id;
    return id;
}

inline std::vector<GameObject*> Ordered(Scene& scene) {
    std::vector<GameObject*> layers;
    for (const auto& object : scene.GetObjects())
        if (!object->IsPendingDestroy() && !object->IsPoolAvailable() && object->GetComponent<SceneLayer>())
            layers.push_back(object.get());
    auto rank = [](GameObject* object) {
        auto* layer = object->GetComponent<SceneLayer>();
        return layer->Required() ? layer->role : int(SceneLayer::Game);
    };
    std::stable_sort(layers.begin(), layers.end(), [&](auto* a, auto* b) { return rank(a) < rank(b); });
    return layers;
}

inline GameObject& Create(Scene& scene, const std::string& name, EntityID id = 0, SceneLayer::Role role = SceneLayer::Game) {
    auto& layer = scene.CreateGameObject(id && role == SceneLayer::Game ? scene.UniqueObjectName(name) : name, id);
    if (role != SceneLayer::Game) layer.name = name;
    layer.isGroup = true;
    layer.AddComponent<SceneLayer>().role = role;
    return layer;
}

inline GameObject& CreateDefaults(Scene& scene, EntityID firstID = 0) {
    auto create = [&](const char* name, SceneLayer::Role role) -> GameObject& {
        while (firstID && scene.Find(firstID)) ++firstID;
        return Create(scene, name, firstID ? firstID++ : 0, role);
    };
    auto& system = create("System", SceneLayer::System);
    create("UI", SceneLayer::UI);
    create("Game", SceneLayer::Game);
    return system;
}

inline bool Eligible(const GameObject& layer) {
    return layer.isGroup && layer.IsActive() && std::abs(glm::determinant(layer.GetWorldMatrix())) >= 1e-8f;
}

inline GameObject* PlacementLayer(Scene& scene, GameObject* selected = nullptr) {
    for (auto* p = selected; p; p = p->GetHierarchyParent())
        if (p->GetComponent<SceneLayer>() && Eligible(*p)) return p;
    // The named default covers normal placement without walking all scene objects.
    bool hasLayers = false;
    GameObject* named = nullptr;
    scene.EachComponent<SceneLayer>([&](SceneLayer& marker) {
        auto& layer = marker.GetGameObject();
        if (layer.IsPooled()) return;
        hasLayers = true;
        if (!marker.Required() && layer.name == "Game" && Eligible(layer)) named = &layer;
    });
    if (named || !hasLayers) return named;
    for (auto* layer : Ordered(scene))
        if (!layer->IsPooled() && !layer->IsRequiredLayer() && Eligible(*layer)) return layer;
    return &Create(scene, "Game", NextLayerID(scene));
}

inline void PlaceRoot(GameObject& root, GameObject* selected = nullptr) {
    if (root.GetHierarchyParent() || root.GetComponent<SceneLayer>()) return;
    if (auto* layer = PlacementLayer(root.GetScene(), selected)) root.SetGroup(layer, false);
}

inline void Migrate(Scene& scene) {
    GameObject *system = nullptr, *ui = nullptr, *game = nullptr;
    auto layers = Ordered(scene);
    std::vector<GameObject*> legacy;
    std::unordered_set<GameObject*> sharedParents;
    for (const auto& object : scene.GetObjects())
        if (object->GetParent() && object->GetGroup()) {
            sharedParents.insert(object->GetParent());
            sharedParents.insert(object->GetGroup());
        }
    bool hadLegacy = false;
    for (auto* layer : layers) {
        auto* marker = layer->GetComponent<SceneLayer>();
        layer->isGroup = true;
        if (marker->role == SceneLayer::Legacy) {
            hadLegacy = true;
            // A transformed, inactive or component-bearing legacy node is part of the
            // authored hierarchy. Keep it and its bindings intact as an ordinary group.
            bool hasContent = false;
            layer->EachComponent([&](Component& component) { if (&component != marker) hasContent = true; });
            if (hasContent || !layer->activeSelf || layer->GetWorldMatrix() != Matrix4(1) || sharedParents.contains(layer)) {
                layer->RemoveComponent(*marker);
                layer->FlushComponents();
            } else {
                legacy.push_back(layer);
            }
        } else if (marker->role == SceneLayer::System && !system) {
            system = layer;
            layer->name = "System";
        } else if (marker->role == SceneLayer::UI && !ui) {
            ui = layer;
            layer->name = "UI";
        } else {
            marker->role = SceneLayer::Game;
            if (!game || layer->name == "Game") game = layer;
        }
    }

    auto promote = [&](GameObject*& destination, SceneLayer::Role role, const char* name,
                       std::initializer_list<const char*> aliases) {
        if (destination) return;
        for (const char* alias : aliases) {
            auto match = std::find_if(legacy.begin(), legacy.end(), [&](auto* candidate) {
                return candidate->name == alias && candidate->GetComponent<SceneLayer>()->role == SceneLayer::Legacy;
            });
            if (match == legacy.end()) continue;
            destination = *match;
            destination->name = role == SceneLayer::Game ? scene.UniqueObjectName(name, destination) : name;
            destination->GetComponent<SceneLayer>()->role = role;
            return;
        }
        destination = &Create(scene, name, NextLayerID(scene), role);
    };
    promote(system, SceneLayer::System, "System", {"System", "System Layer"});
    promote(ui, SceneLayer::UI, "UI", {"UI", "Layer 1"});
    if (layers.empty() || hadLegacy)
        promote(game, SceneLayer::Game, "Game", {"Game", "Layer 2", "Layer 0"});

    std::unordered_set<GameObject*> oldLayers(legacy.begin(), legacy.end());
    std::vector<GameObject*> redundant;
    for (auto* layer : legacy) {
        auto* marker = layer->GetComponent<SceneLayer>();
        if (marker->role != SceneLayer::Legacy) continue;
        if (layer->name == "Layer 0" || layer->name == "Layer 1" || layer->name == "Layer 2")
            redundant.push_back(layer);
        marker->role = SceneLayer::Game;
    }
    if (!legacy.empty() && !Eligible(*game)) game = &Create(scene, "Game", NextLayerID(scene));
    // Preserve IDs, local transforms and descendants. Only the edge from a model
    // root to a former layer changes. A saved group offset can be nonidentity.
    for (const auto& object : scene.GetObjects()) {
        if (object->GetComponent<SceneLayer>()) continue;
        auto* parent = object->GetHierarchyParent();
        if (!parent || !oldLayers.contains(parent) || parent == game) continue;
        auto base = object->GetParentMatrix();
        object->SetParent(nullptr);
        object->SetGroup(game, false);
        object->groupOffset = glm::inverse(game->GetWorldMatrix()) * base;
    }
    for (auto* layer : redundant) scene.Destroy(layer->GetID());
    if (!redundant.empty()) scene.FlushDestroyed();

    std::vector<GameObject*> roots;
    for (const auto& object : scene.GetObjects())
        if (!object->GetComponent<SceneLayer>() && !object->GetHierarchyParent()) roots.push_back(object.get());
    if (roots.empty()) return;
    auto* target = PlacementLayer(scene);
    for (auto* root : roots) root->SetGroup(target, false);
}
}
