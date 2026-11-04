#pragma once
#include "Project/Assets.h"
#include "EditorEditUI.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Rice {
inline bool DrawMaterialFields(SurfaceMaterial& value,AssetDatabase* assets){
    bool changed=ImGui::Checkbox("Enabled",&value.enabled);
    changed|=ImGui::SliderFloat("Metalicity",&value.metallicity,0.f,1.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    changed|=ImGui::SliderFloat("Roughness",&value.roughness,0.f,1.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    std::string display=value.albedoAsset.empty()?"None":"Missing asset";
    if(assets)for(const auto& entry:assets->Entries())if(entry.id==value.albedoAsset){auto text=entry.path.generic_u8string();display.assign(text.begin(),text.end());}
    if(ImGui::BeginCombo("Albedo / Cubemap",display.c_str())){
        if(ImGui::Selectable("None",value.albedoAsset.empty())){changed|=!value.albedoAsset.empty();value.albedoAsset.clear();}
        if(assets)for(const auto& entry:assets->Entries()){
            auto text=entry.path.generic_u8string();std::string label(text.begin(),text.end());
            if(ImGui::Selectable(label.c_str(),value.albedoAsset==entry.id)){changed|=value.albedoAsset!=entry.id;value.albedoAsset=entry.id;}
        }
        ImGui::EndCombo();
    }
    if(value.albedoTexture)ImGui::Text("Detected: %s",CubemapLayoutName(DetectCubemapLayout(value.albedoTexture->GetWidth(),value.albedoTexture->GetHeight())));
    return changed;
}
struct MaterialOverwritePrompt {
    EntityID target=0;
    uint64_t scene=0;
    SurfaceMaterial proposed;
    bool remove=false;
    bool Pending(const Scene& current,const GameObject& object)const{return target==object.GetID()&&scene==current.GetInstanceID();}
    void Queue(const Scene& current,const GameObject& object,const SurfaceMaterial& value,bool detach=false){target=object.GetID();scene=current.GetInstanceID();proposed=value;remove=detach;}
    void Return(){target=0;scene=0;remove=false;}
    bool Proceed(Scene& current,GameObject& object){
        if(!Pending(current,object))return false;
        OverwriteTriangleMaterials(object,proposed);if(remove)object.RemoveComponent<Material>();Return();return true;
    }
};
// The same fields are used by the object Inspector and the selected triangle.
// A proposal is never applied to the scene until the overwrite is accepted.
inline bool DrawMaterialInspector(Scene& scene,GameObject& object,AssetDatabase* assets,MaterialOverwritePrompt& prompt,const BeforeEditorEdit& beforeEdit={}){
    if(!prompt.Pending(scene,object)){
        auto value=MaterialSettings(object.GetComponent<Material>());
        if(DrawMaterialFields(value,assets)){
            if(HasTriangleMaterials(object)){prompt.Queue(scene,object,value);ImGui::ClearActiveID();}
            else {if(beforeEdit)beforeEdit();ApplyMaterialSettings(*object.GetComponent<Material>(),value);}
        }
        return false;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.24f,.10f,.08f,1));
    ImGui::BeginChild("Overwrite warning",{0,116},ImGuiChildFlags_Borders);
    ImGui::TextWrapped("all edits from the shape editor will be overwritten!");
    ImGui::Spacing();bool proceed=ImGui::Button("Proceed");ImGui::SameLine();bool cancel=ImGui::Button("Return");
    ImGui::EndChild();ImGui::PopStyleColor();
    if(proceed){if(beforeEdit)beforeEdit();return prompt.Proceed(scene,object);}
    if(cancel)prompt.Return();return false;
}
}
