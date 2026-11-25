#include "Streaming/StreamingPanel.h"
#include "Streaming/SlotModel.h"
#include "Engine/Data/DataValueCodec.h"
#include <imgui.h>
#include <chrono>
#include <iostream>
#include <thread>

namespace D=Rice::Data;
namespace S=RiceController::Streaming;
namespace {
void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void Drain(S::StreamingPanel& panel,RiceController::ProcessManager& processes) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(panel.Busy()) {
        panel.Tick({},processes);
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Slot panel storage timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(panel.Ready(),"Slot panel did not load persisted settings");
}
}
int main(int argc,char** argv) {
    ImGuiContext* gui=nullptr;
    try {
        const auto root=std::filesystem::path(argc>1?argv[1]:"TestData/StreamingPanel") /
            RiceController::NewId();
        std::filesystem::create_directories(root);
        const auto database=root/"controller.sqlite3";
        {
            D::SqliteStore store(database);
            S::SlotRegistry registry;
            S::Slot slot;slot.slotId="smoke-slot";slot.name="방송 테스트";
            registry.Add(slot);registry.ConfigureInstance("smoke-slot","entry-1","game-1");
            D::DataRecord settings;
            settings.key.domain="controller";settings.key.build="controller";settings.key.dataId="streaming.slots";
            settings.name="Streaming slots";settings.value=D::EncodeValue(registry.Serialize());store.Put(settings,0);
            D::DataRecord score;
            score.key={D::Scope::Build,D::Lifetime::Slot,"local","game-1","smoke-slot","score"};
            score.name="점수";score.description="잠긴 데이터도 운영자가 편집할 수 있습니다.";
            score.value=D::EncodeValue(int32_t(42));store.Put(score,0);
        }
        RiceController::ProcessManager processes;
        {
            S::StreamingPanel firstLaunch(root/"new-user"/"controller.sqlite3");
            Drain(firstLaunch,processes);
            Check(!firstLaunch.HasSlotForEntry("entry-1"),"First launch should start with an empty slot list");
        }
        S::StreamingPanel panel(database);Drain(panel,processes);
        Check(panel.HasSlotForEntry("entry-1"),"Persisted slot assignment missing");
        panel.OpenManagement("smoke-slot");Drain(panel,processes);
        gui=ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1440,1000};io.DeltaTime=1.0f/60.0f;
        unsigned char* pixels=nullptr;int width=0,height=0;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        Check(pixels && width>0 && height>0,"Font atlas unavailable");
        for(int i=0;i<4;++i) {
            ImGui::NewFrame();ImGui::SetNextWindowSize({1380,700});ImGui::Begin("Controller test");
            panel.Draw({},processes,false);ImGui::End();panel.DrawWindows({},processes);ImGui::Render();
            Check(ImGui::GetDrawData()->TotalVtxCount>0,"Slot/settings UI did not produce draw data");
        }
        ImGui::DestroyContext(gui);gui=nullptr;
        std::cout<<"Persisted slot and settings panel rendering passed\n";
        return 0;
    }catch(const std::exception& error){if(gui)ImGui::DestroyContext(gui);std::cerr<<error.what()<<'\n';return 1;}
}
