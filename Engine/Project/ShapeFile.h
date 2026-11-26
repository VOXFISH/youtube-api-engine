#pragma once
#include "Assets.h"
#include <locale>

namespace Rice {
struct ShapeDocument {
    ShapeMesh mesh;
    TriangleMaterials triangleMaterials; // Scene-only, deliberately excluded from .riceshape files.
    Vector3 pivot{.5f};
    bool hasAnchor=false,anchorEnabled=true,anchorShown=true;
    Transform anchor;
    bool hasFollower=false,followerEnabled=true,followerShown=true;
    Transform follower;
    EntityID followerCamera=0; // Scene-only; never written to an asset.
    static ShapeDocument Capture(GameObject& object){
        ShapeDocument doc;auto* mesh=object.GetComponent<MeshRenderer>();doc.mesh=mesh->custom;doc.triangleMaterials=mesh->triangleMaterials;doc.pivot=object.transform.pivot;
        if(auto* a=object.GetComponent<Anchor>()){doc.hasAnchor=true;doc.anchorEnabled=a->enabled;doc.anchorShown=a->showInEditor;doc.anchor=a->transform;}
        if(auto* f=object.GetComponent<CameraFollower>()){doc.hasFollower=true;doc.followerEnabled=f->enabled;doc.followerShown=f->showInEditor;doc.follower=f->transform;doc.followerCamera=f->camera;}
        return doc;
    }
    void Apply(GameObject& object)const{
        auto& custom=object.GetComponent<MeshRenderer>()->custom;auto revision=custom.revision;custom=mesh;custom.revision=revision+1;
        object.GetComponent<MeshRenderer>()->triangleMaterials=triangleMaterials;
        object.transform.pivot=pivot;
        if(hasFollower){auto& f=object.AddComponent<CameraFollower>();f.enabled=followerEnabled;f.showInEditor=followerShown;f.transform=follower;f.camera=followerCamera;}
        else if(object.GetComponent<CameraFollower>()){object.RemoveComponent<CameraFollower>();object.FlushComponents();}
        if(hasAnchor){auto& a=object.AddComponent<Anchor>();a.enabled=anchorEnabled;a.showInEditor=anchorShown;a.transform=anchor;}
        else if(object.GetComponent<Anchor>()){object.RemoveComponent<Anchor>();object.FlushComponents();}
    }
};
// Mesh and local editing properties, independent of scene IDs and materials.
class ShapeFile {
    static constexpr size_t MaxBytes=16*1024*1024;
public:
    static std::string Encode(const ShapeDocument& doc){
        RICE_BENCHMARK_FUNCTION();
        const auto& mesh=doc.mesh;
        mesh.Validate();if(mesh.Empty())throw std::runtime_error("Cannot save an empty shape");
        auto finite=[](Vector3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        if(!finite(doc.pivot)||!finite(doc.anchor.position)||!finite(doc.anchor.rotation)||!finite(doc.anchor.localScale))throw std::runtime_error("Invalid shape property");
        std::ostringstream output;output.imbue(std::locale::classic());
        const int version=!mesh.normals.empty()?5:mesh.faceGroups.empty()?3:4;
        output<<"RICE_SHAPE "<<version<<'\n'<<std::setprecision(std::numeric_limits<float>::max_digits10);
        mesh.Write(output);output<<'\n'<<doc.pivot.x<<' '<<doc.pivot.y<<' '<<doc.pivot.z<<' '<<doc.hasAnchor<<' '<<doc.anchorEnabled<<' '<<doc.anchorShown<<' ';
        for(auto p:{doc.anchor.position,doc.anchor.rotation,doc.anchor.localScale})output<<p.x<<' '<<p.y<<' '<<p.z<<' ';
        if(!finite(doc.follower.position)||!finite(doc.follower.rotation)||!finite(doc.follower.localScale))throw std::runtime_error("Invalid follower transform");
        output<<doc.hasFollower<<' '<<doc.followerEnabled<<' '<<doc.followerShown<<' ';
        for(auto p:{doc.follower.position,doc.follower.rotation,doc.follower.localScale})output<<p.x<<' '<<p.y<<' '<<p.z<<' ';
        output<<'\n';auto data=output.str();
        if(data.size()>MaxBytes)throw std::runtime_error("Shape exceeds 16 MiB");
        return data;
    }
    static ShapeDocument DecodeDocument(const std::string& data){
        RICE_BENCHMARK_FUNCTION();
        if(data.size()>MaxBytes)throw std::runtime_error("Shape exceeds 16 MiB");
        std::istringstream input(data);input.imbue(std::locale::classic());
        std::string magic;int version=0;input>>magic>>version;
        if(!input||magic!="RICE_SHAPE"||version<1||version>5)throw std::runtime_error("Choose a valid Rice Shape (.riceshape) file");
        ShapeDocument doc;doc.mesh.Read(input);
        if(version>=2){
            input>>doc.pivot.x>>doc.pivot.y>>doc.pivot.z>>doc.hasAnchor>>doc.anchorEnabled>>doc.anchorShown;
            for(auto* p:{&doc.anchor.position,&doc.anchor.rotation,&doc.anchor.localScale})input>>p->x>>p->y>>p->z;
            if(!input)throw std::runtime_error("Truncated shape properties");
        }
        if(version>=3){input>>doc.hasFollower>>doc.followerEnabled>>doc.followerShown;for(auto* p:{&doc.follower.position,&doc.follower.rotation,&doc.follower.localScale})input>>p->x>>p->y>>p->z;if(!input)throw std::runtime_error("Truncated follower properties");}
        input>>std::ws;
        if(!input.eof())throw std::runtime_error("Unexpected data after shape");
        Encode(doc);return doc;
    }
    static std::string Encode(const ShapeMesh& mesh){ShapeDocument doc;doc.mesh=mesh;return Encode(doc);}
    static ShapeMesh Decode(const std::string& data){return DecodeDocument(data).mesh;}
    static void Save(const fs::path& path,const ShapeDocument& doc){
        RICE_BENCHMARK_FUNCTION();
        if(path.empty()||!path.has_filename())throw std::runtime_error("Choose a shape filename");
        AtomicWrite(path,Encode(doc));
    }
    static void Save(const fs::path& path,const ShapeMesh& mesh){ShapeDocument doc;doc.mesh=mesh;Save(path,doc);}
    static ShapeDocument LoadDocument(const fs::path& path){
        if(fs::file_size(path)>MaxBytes)throw std::runtime_error("Shape exceeds 16 MiB");return DecodeDocument(ReadFile(path));
    }
    static ShapeMesh Load(const fs::path& path){
        RICE_BENCHMARK_FUNCTION();
        return LoadDocument(path).mesh;
    }
};
}
