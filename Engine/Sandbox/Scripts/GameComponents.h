#pragma once
#include "Rice/Benchmark.h"
#include "Project/SceneCodec.h"
#include "Streaming/StreamReadiness.h"
#include "Engine/Data/DataContext.h"
#include "PresentationMotion.h"
// Game-specific components are compiled into both the editor and player.
struct Spin : Rice::MonoBehaviour {
    float degreesPerSecond=45;
    void Update(float deltaTime) override {
        RICE_BENCHMARK_FUNCTION();
        auto& angle=GetGameObject().transform.rotation.y;
        angle=std::fmod(angle+degreesPerSecond*deltaTime,360.f);
    }
};
// Optional example script: attach only to games whose Start-time setup is
// sufficient. Games with asynchronous loading call ReadyForStream themselves.
struct SignalStreamReady : Rice::MonoBehaviour {
    void Start() override { Rice::Streaming::ReadyForStream(); }
};
// Optional data-routing example. A controlled stream launch supplies its main
// slot before Start; reads/writes go directly through the SQLite field proxy.
struct StreamDataCounter : Rice::MonoBehaviour {
    std::string databasePath;
    Rice::Data::MemoryStore game;
    std::unique_ptr<Rice::Data::SqliteStore> store;
    std::unique_ptr<Rice::Data::DataContext> context;
    std::optional<Rice::Data::Field<int>> counter;
    void Start() override {
        if(databasePath.empty() || Rice::Data::DataContext::PlayerMainSlot().empty())return;
        store=std::make_unique<Rice::Data::SqliteStore>(std::filesystem::path(std::u8string(databasePath.begin(),databasePath.end())));
        game.BeginGame("local","stream-sample","player");
        context=std::make_unique<Rice::Data::DataContext>(Rice::Data::DataIdentity{"local","stream-sample","player"},game,*store);
        counter.emplace(context->Bind<int,Rice::Data::Scope::Build,Rice::Data::Lifetime::Slot>("counter",0,"Counter"));
        ++*counter;
    }
    void OnDestroy() override {if(counter){++*counter;counter.reset();}context.reset();store.reset();}
};
inline void RegisterGameComponents(Rice::ComponentRegistry& registry){
    RICE_BENCHMARK_FUNCTION();
    RegisterPresentationComponents(registry);
    registry.Register<Spin>("Spin",[](Spin& spin,const Rice::PropertyVisitor& fields){ RICE_BENCHMARK_FUNCTION(); fields("Degrees per second",&spin.degreesPerSecond);});
    registry.Register<SignalStreamReady>("SignalStreamReady",[](auto&,const auto&){});
    registry.Register<StreamDataCounter>("StreamDataCounter",[](auto& component,const auto& fields){fields("Database path",&component.databasePath);});
}
