#pragma once
#include "Rice/Benchmark.h"
#include "Project/SceneCodec.h"
// Game-specific components are compiled into both the editor and player.
struct Spin : Rice::MonoBehaviour {
    float degreesPerSecond=45;
    void Update(float deltaTime) override {
        RICE_BENCHMARK_FUNCTION();
        auto& angle=GetGameObject().transform.rotation.y;
        angle=std::fmod(angle+degreesPerSecond*deltaTime,360.f);
    }
};
inline void RegisterGameComponents(Rice::ComponentRegistry& registry){
    RICE_BENCHMARK_FUNCTION();
    registry.Register<Spin>("Spin",[](Spin& spin,const Rice::PropertyVisitor& fields){ RICE_BENCHMARK_FUNCTION(); fields("Degrees per second",&spin.degreesPerSecond);});
}
