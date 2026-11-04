#pragma once
#include "Renderer/Texture.h"
#include <map>
#include <string>
#include <iomanip>
#include <cmath>
#include <stdexcept>

namespace Rice {
struct SurfaceMaterial {
    bool enabled=true;
    float metallicity=0.f,roughness=.5f;
    std::string albedoAsset;
    Ref<Texture2D> albedoTexture;
    std::string loadedAsset;
    bool operator==(const SurfaceMaterial& other)const{
        return enabled==other.enabled&&metallicity==other.metallicity&&roughness==other.roughness&&albedoAsset==other.albedoAsset;
    }
    void Validate()const{
        if(!std::isfinite(metallicity)||!std::isfinite(roughness)||metallicity<0||metallicity>1||roughness<0||roughness>1||albedoAsset.size()>65536)
            throw std::runtime_error("Invalid triangle material");
    }
};
// Overrides belong to an object, never to a shared mesh or texture asset.
struct TriangleMaterials {
    std::map<uint32_t,SurfaceMaterial> faces;
    bool Edited()const{return !faces.empty();}
    bool HasEnabled()const{for(const auto& [index,value]:faces)if(value.enabled)return true;return false;}
    const SurfaceMaterial& At(uint32_t index,const SurfaceMaterial& fallback)const{
        auto found=faces.find(index);return found==faces.end()?fallback:found->second;
    }
    void Write(std::ostream& out)const{
        if(faces.size()>200000)throw std::runtime_error("Too many triangle materials");
        out<<faces.size()<<' ';
        for(const auto& [index,value]:faces){value.Validate();out<<index<<' '<<value.enabled<<' '<<value.metallicity<<' '<<value.roughness<<' '<<std::quoted(value.albedoAsset)<<' ';}
    }
    void Read(std::istream& in){
        size_t count=0;in>>count;if(!in||count>200000)throw std::runtime_error("Invalid triangle material count");
        TriangleMaterials next;
        for(size_t i=0;i<count;++i){uint32_t index=0;SurfaceMaterial value;in>>index>>value.enabled>>value.metallicity>>value.roughness>>std::quoted(value.albedoAsset);
            if(!in||index>=200000||!next.faces.emplace(index,value).second)throw std::runtime_error("Invalid triangle material index");value.Validate();}
        *this=std::move(next);
    }
    void Validate(size_t triangles)const{
        if(!faces.empty()&&faces.rbegin()->first>=triangles)throw std::runtime_error("Triangle material is outside the mesh");
    }
    template<class F>void Runs(uint32_t triangles,const SurfaceMaterial& fallback,F draw)const{
        if(!triangles)return;
        if(faces.empty()){draw(0,triangles*3,fallback);return;}
        uint32_t first=0;const auto* previous=&At(0,fallback);
        for(uint32_t i=1;i<triangles;++i){const auto& next=At(i,fallback);if(next==*previous&&next.albedoTexture==previous->albedoTexture)continue;draw(first*3,(i-first)*3,*previous);first=i;previous=&next;}
        draw(first*3,(triangles-first)*3,*previous);
    }
};
}
