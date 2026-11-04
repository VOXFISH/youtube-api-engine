#include <Scene/Components.h>
#include <stdexcept>
struct Probe : Rice::Component {
    int calls = 0;
    void Awake() override { calls = 1; }
    void Start() override { if (calls != 1) throw std::runtime_error("order"); calls = 2; }
    void Update(float) override { ++calls; }
};
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
    return 0;
}
