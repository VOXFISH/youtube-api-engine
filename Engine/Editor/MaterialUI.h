#pragma once
#include "Project/Assets.h"
#include "EditorEditUI.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Rice {
struct MaterialFieldMask {
    bool enabled=false,metallicity=false,roughness=false,albedo=false;
    bool Any()const{return enabled||metallicity||roughness||albedo;}
};
inline MaterialFieldMask DrawMaterialScalars(SurfaceMaterial& value,const MaterialFieldMask& mixed={}){
    MaterialFieldMask changed;
    bool enabled=mixed.enabled?false:value.enabled;
    if(mixed.enabled)ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);
    changed.enabled=ImGui::Checkbox("Enabled",&enabled);
    if(mixed.enabled)ImGui::PopItemFlag();
    if(changed.enabled)value.enabled=enabled;

    auto slider=[](const char* label,float& number,bool mixedValue){
        bool edited=ImGui::SliderFloat(label,&number,0.f,1.f,mixedValue?"Mixed":"%.3f",ImGuiSliderFlags_AlwaysClamp);
        // Clicking the current thumb in a mixed field also assigns that value
        // to the other faces. Ctrl-click enters numeric input first.
        return edited||(mixedValue&&ImGui::IsItemActivated()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&
            !ImGui::TempInputIsActive(ImGui::GetItemID()));
    };
    changed.metallicity=slider("Metalicity",value.metallicity,mixed.metallicity);
    changed.roughness=slider("Roughness",value.roughness,mixed.roughness);
    return changed;
}
inline bool DrawMaterialAlbedo(SurfaceMaterial& value,AssetDatabase* assets,bool mixed=false,bool reapply=false){
    bool changed=false;
    std::string display=mixed?"Mixed":value.albedoAsset.empty()?"None":"Missing asset";
    if(assets&&!mixed)for(const auto& entry:assets->Entries())if(entry.id==value.albedoAsset){auto text=entry.path.generic_u8string();display.assign(text.begin(),text.end());}
    if(ImGui::BeginCombo("Albedo / Cubemap",display.c_str())){
        if(ImGui::Selectable("None",!mixed&&value.albedoAsset.empty())){changed=mixed||!value.albedoAsset.empty();value.albedoAsset.clear();}
        if(assets)for(const auto& entry:assets->Entries()){
            auto text=entry.path.generic_u8string();std::string label(text.begin(),text.end());
            if(ImGui::Selectable(label.c_str(),!mixed&&value.albedoAsset==entry.id)){changed=reapply||mixed||value.albedoAsset!=entry.id;value.albedoAsset=entry.id;}
        }
        ImGui::EndCombo();
    }
    if(!mixed&&!changed&&value.albedoTexture)ImGui::Text("Detected: %s",CubemapLayoutName(DetectCubemapLayout(value.albedoTexture->GetWidth(),value.albedoTexture->GetHeight())));
    return changed;
}
inline bool DrawMaterialFields(SurfaceMaterial& value,AssetDatabase* assets){
    auto changed=DrawMaterialScalars(value);
    changed.albedo=DrawMaterialAlbedo(value,assets);
    return changed.Any();
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
