#include <Scene/Components.h>
#include <algorithm>
#include <stdexcept>
#include <vector>
struct Probe : Rice::Component {
    int calls = 0;
    void Awake() override { calls = 1; }
    void Start() override { if (calls != 1) throw std::runtime_error("order"); calls = 2; }
    void Update(float) override { ++calls; }
};

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Value : Rice::Component {
    int number = 0;
};
struct FirstValue : Value {};
struct SecondValue : Value {};

struct ReplaceOnDestroy : Rice::Component {
    bool replace = true;
    bool visibleDuringAwake = false;
    void Awake() override {
        visibleDuringAwake = GetGameObject().GetComponent<ReplaceOnDestroy>() == this;
    }
    void OnDestroy() override {
        if (replace) GetGameObject().AddComponent<ReplaceOnDestroy>().replace = false;
    }
};

void CheckComponentStorage() {
    constexpr Rice::EntityID sparseID = (Rice::EntityID{1} << 48) + 31;
    Rice::Scene scene, other;
    auto& first = scene.CreateGameObject("first", sparseID);
    auto& middle = scene.CreateGameObject("middle");
    auto& last = scene.CreateGameObject("last");
    first.AddComponent<Probe>();
    Check(!first.GetComponent<Value>(), "Base lookup misses before any derived component is added");
    auto& firstValue = first.AddComponent<FirstValue>();
    Check(first.GetComponent<Value>() == &firstValue, "A new derived pool invalidates a previous base miss");
    auto& secondValue = first.AddComponent<SecondValue>();
    middle.AddComponent<FirstValue>();
    auto& lastValue = last.AddComponent<FirstValue>();
    lastValue.number = 17;
    auto& separate = other.CreateGameObject("same ID", sparseID).AddComponent<FirstValue>();
    separate.number = 42;

    Check(first.GetComponent<Value>() == &firstValue, "Base lookup uses attachment order");
    Check(&first.AddComponent<Value>() == &firstValue, "Adding an existing base returns the attached component");
    first.RemoveComponent<FirstValue>();
    Check(first.GetComponent<Value>() == &secondValue, "Base lookup skips a removed derived component");
    auto& replacement = first.AddComponent<FirstValue>();
    first.FlushComponents();
    Check(first.GetComponent<FirstValue>() == &replacement, "Flushing a removal preserves its replacement");
    Check(first.GetComponent<Value>() == &secondValue, "Reattached components follow existing components");

    auto& callback = last.AddComponent<ReplaceOnDestroy>();
    Check(callback.visibleDuringAwake, "An attachment is visible to its own Awake callback");
    last.RemoveComponent(callback);
    last.FlushComponents();
    auto* spawned = last.GetComponent<ReplaceOnDestroy>();
    Check(spawned && !spawned->replace && spawned->visibleDuringAwake,
          "Removing a component preserves a replacement attached by OnDestroy");

    auto middleID = middle.GetID();
    scene.Destroy(middleID);
    scene.FlushDestroyed();
    Check(!scene.Find(middleID), "Destroyed entity is absent");
    Check(last.GetComponent<FirstValue>() == &lastValue && lastValue.number == 17,
          "Erasing a middle entry preserves the moved entry's component address and data");
    Check(other.Find(sparseID)->GetComponent<FirstValue>() == &separate && separate.number == 42,
          "Scenes can hold independent components for the same entity ID");

    scene.Destroy(sparseID);
    auto& reused = scene.CreateGameObject("reused", sparseID);
    auto& reusedValue = reused.AddComponent<FirstValue>();
    scene.FlushDestroyed();
    Check(scene.Find(sparseID) == &reused && reused.GetComponent<FirstValue>() == &reusedValue,
          "Deferred destruction keeps a new entity that reused the persistent ID");
}

void CheckDenseIteration() {
    Rice::Scene scene, other;
    auto& first = scene.CreateGameObject("first");
    auto& second = scene.CreateGameObject("second");
    first.AddComponent<FirstValue>();
    second.AddComponent<FirstValue>();
    auto& disabled = scene.CreateGameObject("disabled").AddComponent<FirstValue>();
    disabled.enabled = false;
    scene.CreateGameObject("other type").AddComponent<SecondValue>();
    other.CreateGameObject("separate scene", first.GetID()).AddComponent<FirstValue>();
    auto& pending = scene.CreateGameObject("destroyed");
    pending.AddComponent<FirstValue>();
    scene.Destroy(pending.GetID());

    std::vector<Rice::EntityID> visited;
    Rice::EntityID replacedID = 0;
    scene.EachComponent<FirstValue>([&](FirstValue& component) {
        auto id = component.GetGameObject().GetID();
        visited.push_back(id);
        if (visited.size() == 1) {
            auto& victim = id == first.GetID() ? second : first;
            replacedID = victim.GetID();
            victim.RemoveComponent<FirstValue>();
            victim.FlushComponents();
            victim.AddComponent<FirstValue>();
            scene.CreateGameObject("spawned").AddComponent<FirstValue>();
        }
    });
    Check(visited.size() == 2 && visited[0] != visited[1],
          "Dense traversal visits each surviving initial component once across a swap erase");
    Check(std::find(visited.begin(), visited.end(), disabled.GetGameObject().GetID()) != visited.end() &&
          std::find(visited.begin(), visited.end(), replacedID) == visited.end() &&
          std::find(visited.begin(), visited.end(), pending.GetID()) == visited.end(),
          "Dense traversal includes disabled components and skips removed or pending instances");
    int count = 0;
    scene.EachComponent<FirstValue>([&](FirstValue&) { ++count; });
    Check(count == 4, "New components enter the next traversal");

    auto& mixed = scene.CreateGameObject("both derived types");
    mixed.AddComponent<FirstValue>();
    mixed.AddComponent<SecondValue>();
    int firstValues = 0, secondValues = 0;
    scene.EachComponent<Value>([&](Value& value) {
        if (firstValues + secondValues == 0) {
            auto& addition = scene.CreateGameObject("added during base traversal");
            if (dynamic_cast<FirstValue*>(&value)) addition.AddComponent<SecondValue>();
            else addition.AddComponent<FirstValue>();
        }
        if (dynamic_cast<FirstValue*>(&value)) ++firstValues;
        if (dynamic_cast<SecondValue*>(&value)) ++secondValues;
    });
    Check(firstValues == 5 && secondValues == 2,
          "A base query visits each initial matching component and defers additions to other pools");
}

struct Lifetime : Rice::Component {
    int& onDestroyCalls;
    int& destructors;
    Lifetime(int& callbacks, int& destructions) : onDestroyCalls(callbacks), destructors(destructions) {}
    void OnDestroy() override { ++onDestroyCalls; }
    ~Lifetime() override { ++destructors; }
};

void CheckRemovalDuringVisit() {
    for (bool destroyOwner : {false, true}) {
        int onDestroyCalls = 0, destructors = 0;
        Rice::Scene scene;
        auto& owner = scene.CreateGameObject("visited");
        auto ownerID = owner.GetID();
        owner.AddComponent<Lifetime>(onDestroyCalls, destructors);
        scene.EachComponent<Lifetime>([&](Lifetime& current) {
            if (destroyOwner) {
                scene.Destroy(ownerID);
                scene.FlushDestroyed();
                Check(!scene.Find(ownerID), "The destroyed object is immediately hidden");
            } else {
                owner.RemoveComponent(current);
                owner.FlushComponents();
                Check(!owner.GetComponent<Lifetime>(), "The removed component is immediately hidden");
            }
            Check(onDestroyCalls == 0 && destructors == 0, "A flush must keep the active visitor's reference alive");
            Check(&current.GetGameObject() == &owner, "The component's owner remains accessible during the visit");
        });
        scene.Flush();
        Check(onDestroyCalls == 1 && destructors == 1, "The removed component is destroyed once after the visit");
    }
}
}

int main() {
    Rice::Scene scene;
    auto& object = scene.CreateGameObject("test");
    auto& probe = object.AddComponent<Probe>();
    scene.Update(0.1f);
    if (probe.calls != 3 || object.GetComponent<Probe>() != &probe) return 1;
    object.SetActive(false); scene.Update(0.1f);
    if (probe.calls != 3) return 2;
    object.SetActive(true); scene.Update(0.1f);
    if (probe.calls != 4) return 3;
    object.transform.Translate({1,2,3});
    if (object.transform.GetMatrix()[3].x != 1) return 4;
    if (object.GetComponent<Rice::Transform>() != &object.transform) return 5;
    auto& a = object.AddComponent<Rice::BoxCollider>();
    object.transform.position = {0,0,0};
    auto& second = scene.CreateGameObject("other");
    auto& b = second.AddComponent<Rice::BoxCollider>();
    second.transform.position = {.75f,0,0};
    if (Rice::CollisionSystem::Update(scene) != 1 || !a.overlapping || !b.overlapping) return 6;
    b.enabled = false;
    if (Rice::CollisionSystem::Update(scene) != 0 || a.overlapping || b.overlapping) return 7;
    b.enabled = true; second.transform.position = {5,0,0};
    if (Rice::CollisionSystem::Update(scene) != 0) return 8;
    object.transform.rotation = {0,0,90}; object.transform.localScale = {2,1,1};
    auto bounds = a.GetBounds();
    if (glm::abs(bounds.second.y - 1.f) > .001f) return 9;
    CheckComponentStorage();
    CheckDenseIteration();
    CheckRemovalDuringVisit();
    return 0;
}
