#pragma once
#include "Rice/Benchmark.h"
#include "Scene.h"
#include "EnvironmentRenderer.h"
#include "CameraComponent.h"
#include "ShapeMesh.h"
#include "TriangleMaterials.h"
#include "MeshIntersection.h"
#include "DynamicReflection.h"
#include "ReflectionIntersection.h"
#include "SceneSpatial.h"
#include "ShadowInvalidation.h"
#include "MaterialUV.h"
#include <bit>
#include "Renderer/Renderer.h"
#include "Renderer/Buffer.h"
#include "Renderer/VertexArray.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include <limits>
#include <map>
namespace Rice {
struct MeshRenderer : Component {
    bool castsShadow=true;
    Color color{0.7f,0.7f,0.8f,1};
    int shape = int(BasicShape::Cube);
    ShapeMesh custom;
    std::string shapeAsset;
    TriangleMaterials triangleMaterials;
    mutable uint64_t boundsRevision=0;
    mutable const Vector3* boundsPositions=nullptr;
    mutable size_t boundsPositionCount=0;
    mutable std::pair<Vector3,Vector3> cachedLocalBounds;
    std::optional<std::pair<Vector3,Vector3>> GeometryBounds()const override{
        if(shape==int(BasicShape::Custom)){
            if(!GameObject::WorldCacheEnabled())return custom.Bounds();
            if(boundsRevision!=custom.revision||boundsPositions!=custom.positions.data()||boundsPositionCount!=custom.positions.size()){
                cachedLocalBounds=custom.Bounds();boundsRevision=custom.revision;
                boundsPositions=custom.positions.data();boundsPositionCount=custom.positions.size();
            }
            return cachedLocalBounds;
        }
        if(shape==int(BasicShape::Circle)||shape==int(BasicShape::Rectangle))return std::pair{Vector3(-.5f,-.5f,0),Vector3(.5f,.5f,0)};
        return std::pair{Vector3(-.5f),Vector3(.5f)};
    }
    Ref<VertexArray> customArray;
    uint64_t uploadedRevision=0;
    MeshIntersection intersection;
    uint64_t intersectionRevision=0;
    const MeshIntersection& Intersection(){
        RICE_BENCHMARK_FUNCTION();
        if(intersectionRevision!=custom.revision){intersection.Rebuild(custom);intersectionRevision=custom.revision;}
        return intersection;
    }
    void SetShape(int next){
        RICE_BENCHMARK_FUNCTION();
        if(next==int(BasicShape::Custom)&&custom.Empty())custom=ShapeMesh::FromPrimitive(BasicShape(std::clamp(shape,0,5)));
        shape=next;
    }
};
struct ObjectScript : MonoBehaviour { std::string scriptFile; };
struct BlankScript : MonoBehaviour { std::string scriptFile; };
struct SingletonManager : MonoBehaviour {
    std::string scriptFile;
    static SingletonManager* Instance(Scene& scene) {
        RICE_BENCHMARK_FUNCTION();
        for (auto& object : scene.GetObjects()) {
            auto* manager = object->GetComponent<SingletonManager>();
            if (object->IsActive() && manager && manager->enabled) return manager;
        }
        return nullptr;
    }
    bool IsInstance() { RICE_BENCHMARK_FUNCTION(); return Instance(GetGameObject().GetScene()) == this; }
};
struct Material : Component {
    float metallicity = 0.f;
    float roughness = .5f;
    std::string albedoAsset;
    Ref<Texture2D> albedoTexture;
    std::string loadedAsset;
};
inline SurfaceMaterial MaterialSettings(const Material* material){
    SurfaceMaterial result;result.enabled=material&&material->enabled;
    if(material){result.metallicity=material->metallicity;result.roughness=material->roughness;result.albedoAsset=material->albedoAsset;result.albedoTexture=material->albedoTexture;result.loadedAsset=material->loadedAsset;}
    return result;
}
inline void ApplyMaterialSettings(Material& material,const SurfaceMaterial& value){
    material.enabled=value.enabled;material.metallicity=value.metallicity;material.roughness=value.roughness;material.albedoAsset=value.albedoAsset;material.albedoTexture=value.albedoTexture;material.loadedAsset=value.loadedAsset;
}
inline bool HasTriangleMaterials(const GameObject& object){auto* mesh=object.GetComponent<MeshRenderer>();return mesh&&mesh->triangleMaterials.Edited();}
inline void OverwriteTriangleMaterials(GameObject& object,const SurfaceMaterial& value){
    ApplyMaterialSettings(object.AddComponent<Material>(),value);
    if(auto* mesh=object.GetComponent<MeshRenderer>())mesh->triangleMaterials.faces.clear();
}
struct SpriteRenderer : Component {
    bool castsShadow=true;
    Color color{1}; Ref<Texture2D> texture; std::string asset; Vector4 uvRect{0,0,1,1};
    std::optional<std::pair<Vector3,Vector3>> GeometryBounds()const override{return std::pair{Vector3(-.5f,-.5f,0),Vector3(.5f,.5f,0)};}
    bool LoadImage(const std::string& path) {
        RICE_BENCHMARK_FUNCTION();
        auto loaded = Texture2D::Create(path);
        if (!loaded || !loaded->GetWidth()) return false;
        texture = std::move(loaded); return true;
    }
};
struct Light : Component {
    bool castsShadow=true;
    Color color{1}; float intensity=1; Vector3 direction{-1,-1,-1};
    int type=0; // 0: Point, 1: Directional
    float range=10;
    Vector3 WorldPosition()const{ RICE_BENCHMARK_FUNCTION(); return Vector3(GetGameObject().GetWorldMatrix()[3]);}
    Vector3 WorldDirection()const{ RICE_BENCHMARK_FUNCTION(); auto value=GetGameObject().GetWorldRotation()*direction;return glm::length(value)>.0001f?glm::normalize(value):Vector3(0,-1,0);}
};
struct BoxCollider : Component {
    Vector3 center{0}, size{1}; bool overlapping = false, isTrigger = false;
    mutable bool boundsCached=false;
    mutable Matrix4 boundsWorld{1};
    mutable Vector3 boundsCenter{0},boundsSize{0};
    mutable std::pair<Vector3,Vector3> cachedBounds;
    std::pair<Vector3,Vector3> GetBounds() const {
        RICE_BENCHMARK_FUNCTION();
        auto matrix = GetGameObject().GetWorldMatrix();
        if(GameObject::WorldCacheEnabled()&&boundsCached&&boundsWorld==matrix&&boundsCenter==center&&boundsSize==size)return cachedBounds;
        Vector3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
        for (int i=0;i<8;++i) {
            Vector3 corner = center + glm::abs(size)*Vector3(i&1 ? .5f:-.5f,i&2 ? .5f:-.5f,i&4 ? .5f:-.5f);
            Vector3 world(matrix*Vector4(corner,1)); low=glm::min(low,world); high=glm::max(high,world);
        }
        if(GameObject::WorldCacheEnabled()){boundsCached=true;boundsWorld=matrix;boundsCenter=center;boundsSize=size;cachedBounds={low,high};}
        return {low,high};
    }
};
class CollisionSystem {
public:
    static size_t Update(Scene& scene,bool useBVH=true,size_t* comparisons=nullptr) {
        RICE_BENCHMARK_FUNCTION();
        if(comparisons)*comparisons=0;
        std::vector<BoxCollider*> colliders;
        for (auto& obj:scene.GetObjects()) if (auto* c=obj->GetComponent<BoxCollider>()) {
            c->overlapping=false; if(obj->IsActive() && c->enabled) colliders.push_back(c);
        }
        size_t count=0;
        if(useBVH){
            std::vector<SpatialBounds> bounds;bounds.reserve(colliders.size());
            for(auto* collider:colliders){auto [low,high]=collider->GetBounds();bounds.push_back({low,high});}
            SpatialBVH tree;tree.Build(bounds);
            for(size_t i=0;i<colliders.size();++i)tree.QueryOverlaps(bounds[i],[&](size_t j){
                if(j<=i)return;
                if(comparisons)++*comparisons;
                colliders[i]->overlapping=colliders[j]->overlapping=true;++count;
            });
            return count;
        }
        for(size_t i=0;i<colliders.size();++i) for(size_t j=i+1;j<colliders.size();++j) {
            if(comparisons)++*comparisons;
            auto [a,b]=colliders[i]->GetBounds(); auto [c,d]=colliders[j]->GetBounds();
            if(glm::all(glm::lessThanEqual(a,d)) && glm::all(glm::lessThanEqual(c,b))) {
                colliders[i]->overlapping=colliders[j]->overlapping=true; ++count;
            }
        }
        return count;
    }
};
class SceneRenderer {
public:
    void SetShadowsEnabled(bool enabled){shadowsEnabled=enabled;}
    void SetShadowResolution(int resolution){shadowMaps.SetResolution(resolution);}
    const auto& GetShadowStats()const{return shadowMaps.GetStats();}
    void SetShadowCullingEnabled(bool enabled){shadowCullingEnabled=enabled;}
    void SetShadowCachingEnabled(bool enabled){shadowMaps.SetCachingEnabled(enabled);}
    void SetShadowSelectiveInvalidationEnabled(bool enabled){shadowInvalidation.SetSelectiveEnabled(enabled);}
    const auto& GetShadowInvalidationStats()const{return shadowInvalidation.GetStats();}
    struct ShadowDrawStats {size_t candidates=0,submitted=0,drawCalls=0,instances=0;};
    const ShadowDrawStats& GetShadowDrawStats()const{return shadowDrawStats;}
    struct SpatialStats {size_t captureFaces=0,captureCandidates=0,captureSubmitted=0,boundsUpdates=0,builds=0,refits=0;};
    void SetSpatialCullingEnabled(bool enabled){spatialCullingEnabled=enabled;}
    const SpatialStats& GetSpatialStats()const{return spatialStats;}
    struct RenderStats {
        uint32_t drawCalls=0,instancedDrawCalls=0,instancedInstances=0;
    };
    void SetCubeInstancingEnabled(bool enabled) { RICE_BENCHMARK_FUNCTION(); cubeInstancingEnabled=enabled; }
    const RenderStats& GetRenderStats() const { RICE_BENCHMARK_FUNCTION(); return renderStats; }
private:
    ShadowMaps shadowMaps;
    bool shadowsEnabled=true;
    bool shadowCullingEnabled=true;
    ShadowDrawStats shadowDrawStats;
    size_t shadowCasterCount=0;
    std::vector<ShadowLight> preparedLights;
    ShadowInvalidation shadowInvalidation;
    std::vector<uint64_t> lightingStamp;
    uint64_t shadowReflectionVersion=0;
    std::map<EntityID,uint64_t> reflectionShadowVersions;
    Ref<Shader> shadowShader;
    void DrawShadow(const Matrix4& vp){
        if(!shadowShader)shadowShader=Ref<Shader>(Shader::Create(R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 uv;
layout(location=3) in mat4 instanceWorld;layout(location=7) in vec4 instanceColor;
uniform mat4 u_ViewProjection,u_Transform;uniform int u_Instanced;uniform vec4 color,uvRect;
out vec2 UV;out vec3 localNormal,localPosition;out vec4 vertexColor;
void main(){mat4 world=u_Instanced==1?instanceWorld:u_Transform;vertexColor=u_Instanced==1?instanceColor:color;
localNormal=normal;localPosition=position;UV=uvRect.xy+uv*uvRect.zw;gl_Position=u_ViewProjection*world*vec4(position,1);}
)",std::string(R"(#version 330 core
in vec2 UV;in vec4 vertexColor;uniform sampler2D image;uniform int sprite,textured;
)")+MaterialUVGLSL+R"(
void main(){float alpha=vertexColor.a;if(textured==1)alpha*=texture(image,sprite==1?UV:materialUV()).a;if(alpha<.1)discard;}
)"));
        shadowShader->Bind();shadowShader->SetUniformMat4("u_ViewProjection",vp);
        shadowShader->SetUniformInt("image",0);shadowShader->SetUniformInt("u_Instanced",0);
        std::vector<size_t> visible;
        shadowDrawStats.candidates+=shadowCasterCount;
        if(shadowCullingEnabled)spatialCache.Tree().QueryFrustum(vp,[&](size_t i){visible.push_back(i);});
        else for(size_t i=0;i<spatialCache.Entries().size();++i)visible.push_back(i);
        std::vector<CubeInstance> instances;
        for(size_t i:visible){
            const auto& entry=spatialCache.Entries()[i];auto& object=*entry.object;
            if(!glm::all(glm::greaterThan(glm::abs(object.transform.localScale),Vector3(.00001f))))continue;
            auto* mesh=object.GetComponent<MeshRenderer>();auto* sprite=object.GetComponent<SpriteRenderer>();
            auto draw=[&](const Ref<VertexArray>& array,Color color,const Ref<Texture2D>& texture,bool isSprite,Vector4 uv,uint32_t count=0,uint32_t first=0){
                shadowShader->SetUniformMat4("u_Transform",entry.world);shadowShader->SetUniformFloat4("color",color);
                shadowShader->SetUniformInt("textured",texture?1:0);shadowShader->SetUniformInt("sprite",isSprite);
                shadowShader->SetUniformFloat4("uvRect",uv);
                shadowShader->SetUniformInt("cubeLayout",!isSprite&&mesh&&mesh->shape==int(BasicShape::Cube)&&texture?int(DetectCubemapLayout(texture->GetWidth(),texture->GetHeight())):0);
                if(texture)texture->Bind(0);array->Bind();RenderCommand::DrawIndexed(array,count,first);
                ++shadowDrawStats.drawCalls;
            };
            if(mesh&&mesh->enabled&&mesh->castsShadow&&mesh->color.a>=.1f){
                ++shadowDrawStats.submitted;
                auto base=MaterialSettings(object.GetComponent<Material>());
                if(mesh->shape==int(BasicShape::Cube)&&!(base.enabled&&base.albedoTexture))instances.push_back({entry.world,mesh->color});
                else if(mesh->shape==int(BasicShape::Custom)){
                    auto array=CustomArray(*mesh);
                    mesh->triangleMaterials.Runs(static_cast<uint32_t>(mesh->custom.indices.size()/3),base,[&](uint32_t first,uint32_t count,const SurfaceMaterial& value){
                        draw(array,mesh->color,value.enabled?value.albedoTexture:nullptr,false,{0,0,1,1},count,first);
                    });
                }else draw(shapes[std::clamp(mesh->shape,0,5)],mesh->color,base.enabled?base.albedoTexture:nullptr,false,{0,0,1,1});
            }
            if(sprite&&sprite->enabled&&sprite->castsShadow&&sprite->color.a>=.1f){++shadowDrawStats.submitted;draw(quad,sprite->color,sprite->texture,true,sprite->uvRect);}
        }
        if(!instances.empty()){
            cubeInstanceBuffer->SetData(instances.data(),static_cast<uint32_t>(instances.size()*sizeof(CubeInstance)));
            shadowShader->SetUniformInt("u_Instanced",1);shadowShader->SetUniformInt("textured",0);
            shadowShader->SetUniformInt("sprite",0);shadowShader->SetUniformInt("cubeLayout",0);
            instancedCube->Bind();RenderCommand::DrawIndexedInstanced(instancedCube,static_cast<uint32_t>(instances.size()));
            ++shadowDrawStats.drawCalls;shadowDrawStats.instances+=instances.size();
        }
    }
    void PrepareShadows(Scene& scene){
        shadowDrawStats={};shadowCasterCount=0;
        preparedLights.clear();
        if(scene.settings.directionalLight){
            auto direction=scene.settings.lightDirection;if(glm::length(direction)<.0001f)direction={0,-1,0};
            preparedLights.push_back({0,1,Vector3(0),glm::normalize(direction),Vector3(scene.settings.lightColor)*glm::max(scene.settings.lightIntensity,0.f),1.f,shadowsEnabled});
        }
        for(auto& object:scene.GetObjects())if(object->IsActive())if(auto* light=object->GetComponent<Light>();light&&light->enabled&&preparedLights.size()<8)
            preparedLights.push_back({object->GetID(),light->type==1?1:0,light->WorldPosition(),light->WorldDirection(),Vector3(light->color)*glm::max(light->intensity,0.f),std::max(light->range,.001f),shadowsEnabled&&light->castsShadow});
        bool active=std::any_of(preparedLights.begin(),preparedLights.end(),[](const auto& light){return light.castsShadow;});
        ShadowInvalidation::Snapshot snapshot;
        auto& stamp=snapshot.data;auto& textures=snapshot.textures;
        auto scalar=[&](float value){stamp.push_back(std::bit_cast<uint32_t>(value));};
        auto texture=[&](const Ref<Texture2D>& value){stamp.push_back(reinterpret_cast<uintptr_t>(value.get()));if(value)textures.push_back(value);};
        SpatialBounds bounds{Vector3(-1),Vector3(1)};bool haveBounds=false;
        if(active){
            stamp.reserve(scene.GetObjects().size()*32);snapshot.casters.reserve(scene.GetObjects().size());
            PrepareCaptureScene(scene);
            for(const auto& entry:spatialCache.Entries()){
                auto& object=*entry.object;
                if(!haveBounds){bounds=entry.bounds;haveBounds=true;}else{bounds.low=glm::min(bounds.low,entry.bounds.low);bounds.high=glm::max(bounds.high,entry.bounds.high);}
                auto* mesh=object.GetComponent<MeshRenderer>();auto* sprite=object.GetComponent<SpriteRenderer>();
                bool scaled=glm::all(glm::greaterThan(glm::abs(object.transform.localScale),Vector3(.00001f)));
                bool meshCast=scaled&&mesh&&mesh->enabled&&mesh->castsShadow&&mesh->color.a>=.1f;
                bool spriteCast=scaled&&sprite&&sprite->enabled&&sprite->castsShadow&&sprite->color.a>=.1f;
                if(meshCast)++shadowCasterCount;if(spriteCast)++shadowCasterCount;
                if(!meshCast&&!spriteCast)continue;
                ShadowInvalidation::Caster caster{entry.id,entry.bounds,stamp.size(),0,textures.size(),0};
                stamp.push_back(entry.id);for(int c=0;c<4;++c)for(int r=0;r<4;++r)scalar(entry.world[c][r]);
                for(int a=0;a<3;++a)scalar(object.transform.localScale[a]);
                for(int a=0;a<3;++a){scalar(entry.bounds.low[a]);scalar(entry.bounds.high[a]);}
                stamp.push_back(meshCast);stamp.push_back(spriteCast);
                if(meshCast){
                    stamp.push_back(mesh->shape);stamp.push_back(mesh->custom.revision);scalar(mesh->color.a);
                    auto base=MaterialSettings(object.GetComponent<Material>());texture(base.enabled?base.albedoTexture:nullptr);
                    stamp.push_back(mesh->triangleMaterials.faces.size());
                    for(const auto& [index,value]:mesh->triangleMaterials.faces){stamp.push_back(index);texture(value.enabled?value.albedoTexture:nullptr);}
                }
                if(spriteCast){scalar(sprite->color.a);texture(sprite->texture);for(int a=0;a<4;++a)scalar(sprite->uvRect[a]);}
                caster.end=stamp.size();caster.textureEnd=textures.size();snapshot.casters.push_back(caster);
            }
        }
        shadowInvalidation.Update(scene.GetInstanceID(),std::move(snapshot),preparedLights);
        shadowMaps.Update(scene.GetInstanceID(),preparedLights,bounds,shadowInvalidation.Revisions(),[&](size_t,int,const Matrix4& vp){DrawShadow(vp);});
        std::vector<uint64_t> lighting{scene.GetInstanceID(),uint64_t(shadowsEnabled)};
        for(const auto& light:preparedLights){
            lighting.push_back(light.id);lighting.push_back(light.type);lighting.push_back(light.castsShadow);
            for(auto v:{light.position,light.direction,light.color})for(int i=0;i<3;++i)lighting.push_back(std::bit_cast<uint32_t>(v[i]));
            lighting.push_back(std::bit_cast<uint32_t>(light.range));
        }
        if(lighting!=lightingStamp||shadowMaps.GetStats().updates||shadowInvalidation.Changed()){++shadowReflectionVersion;lightingStamp=std::move(lighting);}
    }
    bool spatialCullingEnabled=true;
    SceneSpatialCache spatialCache;
    SpatialStats spatialStats;
    size_t captureRenderableCount=0;
    unsigned captureDepth=0;
    bool capturePrepared=false;
    void PrepareCaptureScene(Scene& scene){
        if(capturePrepared)return;
        captureRenderableCount=0;
        for(auto& object:scene.GetObjects())if(object->IsActive()){
            auto* mesh=object->GetComponent<MeshRenderer>();auto* sprite=object->GetComponent<SpriteRenderer>();
            if((mesh&&mesh->enabled)||(sprite&&sprite->enabled))++captureRenderableCount;
        }
        if(spatialCullingEnabled||shadowsEnabled){
            spatialCache.Prepare(scene,[](const GameObject& object)->std::optional<std::pair<Vector3,Vector3>>{
                std::optional<std::pair<Vector3,Vector3>> bounds;
                if(auto* mesh=object.GetComponent<MeshRenderer>();mesh&&mesh->enabled)bounds=mesh->GeometryBounds();
                if(auto* sprite=object.GetComponent<SpriteRenderer>();sprite&&sprite->enabled){
                    auto b=*sprite->GeometryBounds();
                    if(bounds){bounds->first=glm::min(bounds->first,b.first);bounds->second=glm::max(bounds->second,b.second);}else bounds=b;
                }
                return bounds;
            });
            const auto& stats=spatialCache.GetStats();spatialStats.boundsUpdates=stats.boundsUpdates;spatialStats.builds=stats.builds;spatialStats.refits=stats.refits;
        }
        capturePrepared=true;
    }
    EnvironmentRenderer environment;
    std::map<EntityID,Scope<DynamicReflection>> reflections;
    uint64_t reflectionScene=0;
    Ref<VertexArray> cube,quad;
    std::vector<Ref<VertexArray>> shapes;
    Ref<Shader> shader;
    bool cubeInstancingEnabled=true;
    RenderStats renderStats;
    struct CubeInstance { Matrix4 world;Color color; };
    static_assert(sizeof(CubeInstance)==20*sizeof(float));
    struct CubeMaterial {
        Ref<Texture2D> texture;
        bool active=false;
        float metallicity=0,roughness=.5f;
        bool operator==(const CubeMaterial&) const = default;
    };
    Ref<VertexArray> instancedCube;
    Ref<VertexBuffer> cubeInstanceBuffer;
    std::vector<CubeInstance> cubeInstances;
    void FlushCubeInstances(const CubeMaterial& material) {
        RICE_BENCHMARK_FUNCTION();
        if(cubeInstances.empty())return;
        const auto count=static_cast<uint32_t>(cubeInstances.size());
        cubeInstanceBuffer->SetData(cubeInstances.data(),count*static_cast<uint32_t>(sizeof(CubeInstance)));
        shader->Bind();
        shader->SetUniformInt("u_Instanced",1);
        shader->SetUniformInt("sprite",0);
        shader->SetUniformInt("useMaterial",material.active);
        shader->SetUniformInt("useReflection",0);
        shader->SetUniformInt("flatReflection",0);
        shader->SetUniformFloat("metallicity",material.metallicity);
        shader->SetUniformFloat("roughness",material.roughness);
        shader->SetUniformInt("textured",material.texture?1:0);
        shader->SetUniformInt("image",0);
        shader->SetUniformInt("cubeLayout",material.texture?int(DetectCubemapLayout(material.texture->GetWidth(),material.texture->GetHeight())):0);
        shader->SetUniformFloat4("uvRect",{0,0,1,1});
        if(material.texture)material.texture->Bind(0);
        Renderer::SubmitInstanced(shader,instancedCube,count);
        ++renderStats.drawCalls;
        ++renderStats.instancedDrawCalls;
        renderStats.instancedInstances+=count;
        shader->SetUniformInt("u_Instanced",0);
        cubeInstances.clear();
    }
    static Ref<VertexArray> BuildShape(BasicShape shape) {
        RICE_BENCHMARK_FUNCTION();
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        auto vertex = [&](Vector3 position, Vector3 normal, Vector2 uv) {
            RICE_BENCHMARK_FUNCTION();
            indices.push_back(static_cast<uint32_t>(indices.size()));
            vertices.insert(vertices.end(), {position.x,position.y,position.z,normal.x,normal.y,normal.z,uv.x,uv.y});
        };
        auto triangle = [&](Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc) {
            RICE_BENCHMARK_FUNCTION();
            vertex(a,na,{a.x+.5f,a.y+.5f});
            vertex(b,nb,{b.x+.5f,b.y+.5f});
            vertex(c,nc,{c.x+.5f,c.y+.5f});
        };
        constexpr int segments = 48;
        constexpr int rings = 24;
        constexpr float pi = 3.14159265359f;
        for (int i=0;i<segments;++i) {
            float a=2*pi*i/segments,b=2*pi*(i+1)/segments;
            Vector3 ra(std::cos(a),0,std::sin(a)),rb(std::cos(b),0,std::sin(b));
            if (shape==BasicShape::Circle) {
                triangle({0,0,0},{.5f*std::cos(a),.5f*std::sin(a),0},{.5f*std::cos(b),.5f*std::sin(b),0},{0,0,1},{0,0,1},{0,0,1});
            } else if (shape==BasicShape::Sphere) {
                auto point=[&](float longitude,float latitude) {
                    RICE_BENCHMARK_FUNCTION();
                    return Vector3(std::sin(latitude)*std::cos(longitude),std::cos(latitude),std::sin(latitude)*std::sin(longitude));
                };
                for(int j=0;j<rings;++j) {
                    auto p=point(a,pi*j/rings),q=point(b,pi*j/rings);
                    auto r=point(a,pi*(j+1)/rings),t=point(b,pi*(j+1)/rings);
                    triangle(.5f*p,.5f*q,.5f*r,p,q,r);
                    triangle(.5f*q,.5f*t,.5f*r,q,t,r);
                }
            } else {
                Vector3 bottomA=ra*.5f+Vector3(0,-.5f,0),bottomB=rb*.5f+Vector3(0,-.5f,0);
                triangle({0,-.5f,0},bottomA,bottomB,{0,-1,0},{0,-1,0},{0,-1,0});
                if(shape==BasicShape::Cone) {
                    Vector3 na=glm::normalize(ra+Vector3(0,.5f,0)),nb=glm::normalize(rb+Vector3(0,.5f,0));
                    triangle(bottomA,{0,.5f,0},bottomB,na,glm::normalize(na+nb),nb);
                } else {
                    Vector3 topA=bottomA+Vector3(0,1,0),topB=bottomB+Vector3(0,1,0);
                    triangle(bottomA,topA,bottomB,ra,ra,rb);
                    triangle(bottomB,topA,topB,rb,ra,rb);
                    triangle({0,.5f,0},topB,topA,{0,1,0},{0,1,0},{0,1,0});
                }
            }
        }
        auto array=CreateRef<VertexArray>();
        auto buffer=CreateRef<VertexBuffer>(vertices.data(),static_cast<uint32_t>(vertices.size()*sizeof(float)));
        buffer->SetLayout({{ShaderDataType::Float3,"position"},{ShaderDataType::Float3,"normal"},{ShaderDataType::Float2,"uv"}});
        array->AddVertexBuffer(buffer);
        array->SetIndexBuffer(CreateRef<IndexBuffer>(indices.data(),static_cast<uint32_t>(indices.size())));
        return array;
    }
    static Ref<VertexArray> Build(bool flat) {
        RICE_BENCHMARK_FUNCTION();
        std::vector<float> vertices; std::vector<uint32_t> indices;
        const Vector3 normals[]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        for(int face=0;face<(flat?1:6);++face) {
            Vector3 n=normals[face]; Vector3 u=glm::abs(n.y)>.5f?Vector3(1,0,0):glm::normalize(glm::cross(Vector3(0,1,0),n));
            Vector3 v=glm::cross(n,u);
            for(int k=0;k<4;++k) {
                float x=(k==1||k==2)?1.f:0.f, y=k>=2?1.f:0.f;
                Vector3 p=u*(x-.5f)+v*(y-.5f)+(flat?Vector3(0):n*.5f);
                vertices.insert(vertices.end(),{p.x,p.y,p.z,n.x,n.y,n.z,x,y});
            }
            uint32_t b=face*4; indices.insert(indices.end(),{b,b+1,b+2,b+2,b+3,b});
        }
        auto va=CreateRef<VertexArray>(); auto vb=CreateRef<VertexBuffer>(vertices.data(),static_cast<uint32_t>(vertices.size()*sizeof(float)));
        vb->SetLayout({{ShaderDataType::Float3,"position"},{ShaderDataType::Float3,"normal"},{ShaderDataType::Float2,"uv"}});
        va->AddVertexBuffer(vb); va->SetIndexBuffer(CreateRef<IndexBuffer>(indices.data(),static_cast<uint32_t>(indices.size()))); return va;
    }
public:
    static Ref<VertexArray> CustomArray(MeshRenderer& mesh){
        RICE_BENCHMARK_FUNCTION();
        if(mesh.custom.Empty())mesh.custom=ShapeMesh::FromPrimitive(BasicShape::Cube);
        if(!mesh.customArray||mesh.uploadedRevision!=mesh.custom.revision){
            auto vertices=mesh.custom.RenderVertices();std::vector<uint32_t> indices(mesh.custom.indices.size());
            for(uint32_t i=0;i<indices.size();++i)indices[i]=i;
            auto array=CreateRef<VertexArray>();auto buffer=CreateRef<VertexBuffer>(vertices.data(),static_cast<uint32_t>(vertices.size()*sizeof(float)));
            buffer->SetLayout({{ShaderDataType::Float3,"position"},{ShaderDataType::Float3,"normal"},{ShaderDataType::Float2,"uv"}});
            array->AddVertexBuffer(buffer);array->SetIndexBuffer(CreateRef<IndexBuffer>(indices.data(),static_cast<uint32_t>(indices.size())));
            mesh.customArray=std::move(array);mesh.uploadedRevision=mesh.custom.revision;
        }
        return mesh.customArray;
    }
    SceneRenderer():cube(Build(false)),quad(Build(true)) {
        RICE_BENCHMARK_FUNCTION();
        shapes={cube,BuildShape(BasicShape::Circle),quad,BuildShape(BasicShape::Sphere),BuildShape(BasicShape::Cylinder),BuildShape(BasicShape::Cone)};
        instancedCube=CreateRef<VertexArray>();
        for(const auto& buffer:cube->GetVertexBuffers())instancedCube->AddVertexBuffer(buffer);
        instancedCube->SetIndexBuffer(cube->GetIndexBuffer());
        cubeInstanceBuffer=CreateRef<VertexBuffer>(nullptr,0);
        cubeInstanceBuffer->SetLayout({{ShaderDataType::Mat4,"instanceWorld"},{ShaderDataType::Float4,"instanceColor"}});
        instancedCube->AddVertexBuffer(cubeInstanceBuffer,1);
        shader=Ref<Shader>(Shader::Create(R"(#version 330 core
layout(location=0) in vec3 position; layout(location=1) in vec3 normal; layout(location=2) in vec2 uv;
layout(location=3) in mat4 instanceWorld; layout(location=7) in vec4 instanceColor;
uniform int u_Instanced; uniform vec4 color; out vec4 vertexColor;
uniform mat4 u_ViewProjection,u_Transform; uniform vec4 uvRect; out vec3 N; out vec3 worldPosition; out vec2 UV; out vec3 localNormal; out vec3 localPosition;
void main(){mat4 transform=u_Instanced==1?instanceWorld:u_Transform;vertexColor=u_Instanced==1?instanceColor:color;localNormal=normal;localPosition=position;vec4 world=transform*vec4(position,1);worldPosition=world.xyz;gl_Position=u_ViewProjection*world;N=transpose(inverse(mat3(transform)))*normal;UV=uvRect.xy+uv*uvRect.zw;}
)",std::string(R"(#version 330 core
in vec3 N; in vec3 worldPosition; in vec2 UV; in vec4 vertexColor; out vec4 result;
uniform vec3 lightColor[8], lightDirection[8]; uniform vec4 lightPositionRange[8]; uniform int lightType[8]; uniform int lightCount, sprite, textured; uniform sampler2D image;
uniform int useMaterial, unlit;
uniform int useReflection, flatReflection;
uniform vec4 flatReflectionPlane;
)" )+ReflectionIntersectionGLSL+ShadowSamplingGLSL+R"(
uniform samplerCube environmentMap;
uniform float reflectionMaxLod;
)"+MaterialUVGLSL+R"(
uniform float metallicity, roughness;
uniform vec3 cameraPosition;
const float pi=3.14159265;
float distributionGGX(float nh){
    float alpha=roughness*roughness;
    float a2=alpha*alpha;
    float d=(1.0-nh*nh)+nh*nh*a2;
    return a2/(pi*d*d);
}
float geometrySchlickGGX(float cosine){
    float k=(roughness+1.0)*(roughness+1.0)/8.0;
    return cosine/(cosine*(1.0-k)+k);
}
vec3 cookTorrance(vec3 base,vec3 n,vec3 v,vec3 l,float nl){
    float nv=clamp(dot(n,v),0.0,1.0);
    vec3 h=(v+l)/max(length(v+l),0.00001);
    float nh=clamp(dot(n,h),0.0,1.0);
    vec3 f0=mix(vec3(.04),base,metallicity);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-clamp(dot(v,h),0.0,1.0),5.0);
    float geometry=geometrySchlickGGX(nv)*geometrySchlickGGX(nl);
    vec3 specular=distributionGGX(nh)*geometry*fresnel/max(4.0*nv*nl,0.00001);
    return (1.0-fresnel)*(1.0-metallicity)*base/pi+specular;
}
void main(){vec4 c=vertexColor;if(textured==1)c*=texture(image,sprite==1?UV:materialUV());if(c.a<0.1)discard;
if(sprite==1 || unlit==1){ result=c; return; }
vec3 normal=normalize(N);
vec3 viewOffset=cameraPosition-worldPosition;
vec3 V=viewOffset/max(length(viewOffset),0.00001);
vec3 light=.15*c.rgb;for(int i=0;i<lightCount;i++){
vec3 L=-lightDirection[i];float attenuation=1.0;
if(lightType[i]==0){vec3 offset=lightPositionRange[i].xyz-worldPosition;float distance=length(offset);L=offset/max(distance,0.0001);float falloff=max(1.0-distance/max(lightPositionRange[i].w,0.001),0.0);attenuation=falloff*falloff;}
float nl=max(dot(normal,L),0.0);
if(nl>0.0){
    vec3 response=c.rgb;
    if(useMaterial==1) response=cookTorrance(c.rgb,normal,V,L,nl);
    light+=shadowVisibility(i,worldPosition,normal)*response*lightColor[i]*attenuation*nl;
}
}
if(useMaterial==1 && useReflection==1){
    vec3 f0=mix(vec3(.04),c.rgb,metallicity);
    vec3 reflectionOrigin=worldPosition;
    vec3 reflectionNormal=normal;
    if(flatReflection==1){
        reflectionNormal=flatReflectionPlane.xyz;
        float denominator=dot(-V,reflectionNormal);
        if(abs(denominator)>.00001)
            reflectionOrigin+=(-V)*(-(dot(reflectionOrigin,reflectionNormal)+flatReflectionPlane.w)/denominator);
    }
    float nv=clamp(dot(reflectionNormal,V),0.0,1.0);
    vec3 fresnel=f0+(1.0-f0)*pow(1.0-nv,5.0);
    vec3 direction=correctedReflectionDirection(reflectionOrigin,reflect(-V,reflectionNormal));
    vec3 environment=vec3(0);
    if(dot(direction,direction)>.00001)
        environment=textureLod(environmentMap,direction,roughness*reflectionMaxLod).rgb;
    light+=environment*fresnel;
}
result=vec4(light,c.a);}
)"));
    }
    void RenderShape(MeshRenderer& mesh,const Camera& camera,Material* material=nullptr,Vector3 pivot=Vector3(0)){
        RICE_BENCHMARK_FUNCTION();
        Renderer::BeginScene(camera);shader->Bind();
        shader->SetUniformInt("u_Instanced",0);
        shader->SetUniformInt("unlit",1);shader->SetUniformInt("sprite",0);
        shader->SetUniformInt("shadowMap",3);shader->SetUniformInt("shadowLightCount",0);
        shader->SetUniformInt("environmentMap",1);shader->SetUniformInt("reflectionGeometry",2);
        shader->SetUniformInt("useMaterial",0);shader->SetUniformInt("useReflection",0);
        shader->SetUniformInt("lightCount",0);shader->SetUniformInt("cubeLayout",0);
        shader->SetUniformFloat4("color",mesh.color);shader->SetUniformFloat4("uvRect",{0,0,1,1});
        auto base=MaterialSettings(material);auto array=CustomArray(mesh);auto transform=glm::translate(Matrix4(1),-pivot);
        mesh.triangleMaterials.Runs(static_cast<uint32_t>(mesh.custom.indices.size()/3),base,[&](uint32_t first,uint32_t count,const SurfaceMaterial& value){
            auto texture=value.enabled?value.albedoTexture:nullptr;
            shader->SetUniformInt("textured",texture?1:0);shader->SetUniformInt("image",0);if(texture)texture->Bind(0);
            Renderer::Submit(shader,array,transform,count,first);
        });Renderer::EndScene();
    }
    void Render(Scene& scene,const Camera& camera,bool capturing=false,EntityID excluded=0,bool editorGuides=false) {
        RICE_BENCHMARK_FUNCTION();
        if(!capturing||captureDepth==0){
            spatialStats={};
            capturePrepared=false;
            if(capturing)PrepareCaptureScene(scene);
            PrepareShadows(scene);
        }
        cubeInstances.clear();
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT,viewport);
        struct Receiver { ReflectionProbe* probe;float size; };
        std::map<EntityID,Receiver> receivers;
        std::vector<ReflectionProbe*> probes;
        if(!capturing){
            if(reflectionScene!=scene.GetInstanceID()){
                reflections.clear();
                reflectionShadowVersions.clear();
                reflectionScene=scene.GetInstanceID();
            }
            for(auto& object:scene.GetObjects())if(object->IsActive()){
                auto* candidate=object->GetComponent<ReflectionProbe>();
                if(candidate && candidate->enabled)probes.push_back(candidate);
            }
            std::erase_if(reflections,[&](const auto& entry){
                RICE_BENCHMARK_FUNCTION();
                return std::none_of(probes.begin(),probes.end(),[&](auto* probe){
                    RICE_BENCHMARK_FUNCTION();
                    return probe->GetGameObject().GetID()==entry.first;
                });
            });
            std::erase_if(reflectionShadowVersions,[&](const auto& entry){return !reflections.contains(entry.first);});
            for(auto& object:scene.GetObjects())if(object->IsActive()){
                auto* mesh=object->GetComponent<MeshRenderer>();
                auto* material=object->GetComponent<Material>();
                if(!mesh || !mesh->enabled || (!(material&&material->enabled)&&!mesh->triangleMaterials.HasEnabled()))continue;
                auto world=object->GetWorldMatrix();
                ReflectionProbe* selected=nullptr;
                float closest=std::numeric_limits<float>::max();
                for(auto* candidate:probes){
                    if(&candidate->GetGameObject()==object.get()){selected=candidate;break;}
                    auto offset=Vector3(candidate->GetGameObject().GetWorldMatrix()[3])-Vector3(world[3]);
                    float distance=glm::dot(offset,offset);
                    if(distance<closest){closest=distance;selected=candidate;}
                }
                Vector3 low(-.5f),high(.5f);
                if(mesh->shape==int(BasicShape::Circle)||mesh->shape==int(BasicShape::Rectangle)){low.z=0;high.z=0;}
                if(mesh->shape==int(BasicShape::Custom))std::tie(low,high)=*mesh->GeometryBounds();
                float size=ReflectionPixelSize(world,camera.GetVPMatrix(),Vector2(viewport[2],viewport[3]),low,high);
                receivers.emplace(object->GetID(),Receiver{selected,size});
            }
            std::vector<Vector4> geometry;
            std::vector<EntityID> geometryOwners;
            bool geometryBuilt=false;
            for(auto* probe:probes){
                auto id=probe->GetGameObject().GetID();
                auto& reflection=reflections[id];
                if(!reflection)reflection=CreateScope<DynamicReflection>();
                bool needsUpdate=reflection->SettingsChanged(*probe)||reflectionShadowVersions[id]!=shadowReflectionVersion;
                for(auto& [objectID,receiver]:receivers)
                    if(receiver.probe==probe && receiver.size>0 && receiver.size>=std::max(probe->maxObjectSize,0.f))needsUpdate=true;
                if(!needsUpdate)continue;
                PrepareCaptureScene(scene);
                if(!geometryBuilt){
                    for(auto& object:scene.GetObjects())if(object->IsActive()){
                        auto world=object->GetWorldMatrix();
                        if(std::abs(glm::determinant(world))<1e-8f)continue;
                        auto inverse=glm::inverse(world);
                        float ownerIndex=static_cast<float>(geometryOwners.size());
                        auto append=[&](int shape){
                            RICE_BENCHMARK_FUNCTION();
                            for(int column=0;column<4;++column)geometry.push_back(inverse[column]);
                            geometry.emplace_back(float(shape),ownerIndex,0,0);
                            geometryOwners.push_back(object->GetID());
                        };
                        if(auto* mesh=object->GetComponent<MeshRenderer>();mesh && mesh->enabled){
                            if(mesh->shape==int(BasicShape::Custom)){
                                // Triangle records use the same five-texel stride as primitives.
                                for(size_t t=0;t+2<mesh->custom.indices.size();t+=3){
                                    for(int c=0;c<3;++c)geometry.push_back(world*Vector4(mesh->custom.positions[mesh->custom.indices[t+c]],1));
                                    geometry.emplace_back(0.f);geometry.emplace_back(6.f,ownerIndex,0.f,0.f);geometryOwners.push_back(object->GetID());
                                }
                            }else append(std::clamp(mesh->shape,0,5));
                        }
                        if(auto* sprite=object->GetComponent<SpriteRenderer>();sprite && sprite->enabled)
                            append(int(BasicShape::Rectangle));
                    }
                    geometryBuilt=true;
                }
                reflection->SetGeometry(geometry,geometryOwners);
                reflection->Capture(*probe,[&](const Camera& captureCamera){
                    RICE_BENCHMARK_FUNCTION();
                    struct CaptureScope {unsigned& depth;CaptureScope(unsigned& value):depth(value){++depth;}~CaptureScope(){--depth;}} scope(captureDepth);
                    Render(scene,captureCamera,true,id);
                });
                reflectionShadowVersions[id]=shadowReflectionVersion;
            }
        }
        environment.Draw(scene,camera,quad,editorGuides&&!capturing,&shadowMaps);
        // Recursive reflection captures finish first; expose this view's submissions only.
        renderStats={};
        bool oldSeamless=glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        Renderer::BeginScene(camera); shader->Bind(); int count=0;
        shadowMaps.Bind(shader);
        shader->SetUniformInt("reflectionGroundEnabled",scene.settings.ground);
        shader->SetUniformInt("u_Instanced",0);
        shader->SetUniformInt("unlit",0);
        if(scene.settings.directionalLight){
            auto direction=scene.settings.lightDirection;
            if(glm::length(direction)<.0001f)direction={0,-1,0};
            shader->SetUniformInt("lightType[0]",1);
            shader->SetUniformFloat4("lightPositionRange[0]",{0,0,0,1});
            shader->SetUniformFloat3("lightDirection[0]",glm::normalize(direction));
            shader->SetUniformFloat3("lightColor[0]",Vector3(scene.settings.lightColor)*glm::max(scene.settings.lightIntensity,0.f));++count;
        }
        for(auto& obj:scene.GetObjects()) if(obj->IsActive()) if(auto* l=obj->GetComponent<Light>();l && l->enabled && count<8) {
            Vector3 direction=l->WorldDirection();
            if(glm::length(direction)<.0001f) direction={0,-1,0};
            shader->SetUniformInt("lightType["+std::to_string(count)+"]",l->type==1?1:0);
            shader->SetUniformFloat4("lightPositionRange["+std::to_string(count)+"]",Vector4(l->WorldPosition(),std::max(l->range,.001f)));
            shader->SetUniformFloat3("lightDirection["+std::to_string(count)+"]",glm::normalize(direction));
            shader->SetUniformFloat3("lightColor["+std::to_string(count)+"]",Vector3(l->color)*glm::max(l->intensity,0.f)); ++count;
        }
        shader->SetUniformInt("environmentMap",1);
        shader->SetUniformInt("reflectionGeometry",2);
        shader->SetUniformInt("lightCount",count);
        shader->SetUniformFloat3("cameraPosition",camera.GetPosition());
        CubeMaterial pendingMaterial;
        std::vector<GameObject*> viewObjects;
        std::vector<Matrix4> captureWorlds;
        if(capturing&&spatialCullingEnabled){
            std::vector<size_t> visible;
            spatialCache.Tree().QueryFrustum(camera.GetVPMatrix(),[&](size_t i){visible.push_back(i);});
            std::sort(visible.begin(),visible.end()); // Keep alpha/submission order identical.
            for(size_t i:visible){viewObjects.push_back(spatialCache.Entries()[i].object);captureWorlds.push_back(spatialCache.Entries()[i].world);}
        }else for(auto& object:scene.GetObjects())viewObjects.push_back(object.get());
        if(capturing){
            ++spatialStats.captureFaces;
            spatialStats.captureCandidates+=captureRenderableCount;
            if(auto* object=scene.Find(excluded);object&&object->IsActive()){
                auto* mesh=object->GetComponent<MeshRenderer>();auto* sprite=object->GetComponent<SpriteRenderer>();
                if((mesh&&mesh->enabled)||(sprite&&sprite->enabled))--spatialStats.captureCandidates;
            }
        }
        for(size_t objectIndex=0;objectIndex<viewObjects.size();++objectIndex){
            auto* obj=viewObjects[objectIndex];
            if(obj->GetID()==excluded||!obj->IsActive()||!glm::all(glm::greaterThan(glm::abs(obj->transform.localScale),Vector3(.00001f))))continue;
            auto drawWorld=[&]{return capturing&&spatialCullingEnabled?captureWorlds[objectIndex]:obj->GetWorldMatrix();};
            auto* mesh=obj->GetComponent<MeshRenderer>(); auto* sprite=obj->GetComponent<SpriteRenderer>();
            if(capturing&&((mesh&&mesh->enabled)||(sprite&&sprite->enabled)))++spatialStats.captureSubmitted;
            if(cubeInstancingEnabled && mesh && mesh->enabled && mesh->shape==int(BasicShape::Cube) &&
               !mesh->triangleMaterials.Edited() && !(sprite&&sprite->enabled)){
                auto* material=obj->GetComponent<Material>();
                const bool active=material&&material->enabled;
                auto receiver=receivers.find(obj->GetID());
                const bool reflection=active&&receiver!=receivers.end()&&receiver->second.probe&&receiver->second.size>0;
                if(!reflection){
                    CubeMaterial next{active?material->albedoTexture:nullptr,active,
                        material?std::clamp(material->metallicity,0.f,1.f):0.f,
                        material?std::clamp(material->roughness,.045f,1.f):.5f};
                    if(!cubeInstances.empty()&&(!(next==pendingMaterial)||cubeInstances.size()>=std::numeric_limits<uint32_t>::max()/sizeof(CubeInstance)))FlushCubeInstances(pendingMaterial);
                    pendingMaterial=std::move(next);
                    cubeInstances.push_back({drawWorld(),mesh->color});
                    continue;
                }
            }
            // Preserve submission order across sprites, custom geometry and reflection receivers.
            if(!cubeInstances.empty())FlushCubeInstances(pendingMaterial);
            auto draw=[&](Color color,bool isSprite,Ref<Texture2D> texture,uint32_t indexCount=0,uint32_t firstIndex=0) {
                RICE_BENCHMARK_FUNCTION();
                shader->Bind(); shader->SetUniformFloat4("color",color); shader->SetUniformInt("sprite",isSprite); shader->SetUniformInt("textured",texture?1:0);shader->SetUniformInt("image",0);
                shader->SetUniformInt("cubeLayout",!isSprite && mesh->shape==int(BasicShape::Cube) && texture
                    ?int(DetectCubemapLayout(texture->GetWidth(),texture->GetHeight())):0);
                shader->SetUniformFloat4("uvRect",isSprite?sprite->uvRect:Vector4(0,0,1,1));
                if(texture)texture->Bind(0); Renderer::Submit(shader,isSprite?quad:mesh->shape==int(BasicShape::Custom)?CustomArray(*mesh):shapes[std::clamp(mesh->shape,0,5)],drawWorld(),indexCount,firstIndex);
                ++renderStats.drawCalls;
            };
            if(mesh && mesh->enabled){
                auto* material=obj->GetComponent<Material>();
                bool active=(material && material->enabled)||(mesh->shape==int(BasicShape::Custom)&&mesh->triangleMaterials.HasEnabled());
                shader->SetUniformInt("useMaterial",active);
                auto world=drawWorld();
                auto receiver=receivers.find(obj->GetID());
                ReflectionProbe* probe=receiver==receivers.end()?nullptr:receiver->second.probe;
                float size=receiver==receivers.end()?0.f:receiver->second.size;
                bool receivesReflection=probe && active && size>0;
                bool flat=receivesReflection && mesh->shape!=int(BasicShape::Custom) && size<std::max(probe->maxObjectSize,0.f);
                shader->SetUniformInt("flatReflection",flat);
                if(flat){
                    auto viewOffset=camera.GetPosition()-Vector3(world[3]);
                    Vector3 viewDirection=glm::length(viewOffset)>.00001f?glm::normalize(viewOffset):Vector3(0,0,1);
                    auto normalMatrix=glm::transpose(glm::inverse(Matrix3(world)));
                    Vector3 planeNormal(0,0,1),planePoint(world[3]);
                    float bestFacing=-1.f;
                    for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}){
                        if(mesh->shape==int(BasicShape::Circle)||mesh->shape==int(BasicShape::Rectangle))
                            if(axis!=2)continue;
                        Vector3 localNormal(0);localNormal[axis]=sign;
                        auto candidate=glm::normalize(normalMatrix*localNormal);
                        float facing=glm::dot(candidate,viewDirection);
                        if(facing<=bestFacing)continue;
                        bestFacing=facing;planeNormal=candidate;
                        Vector3 localPoint=localNormal*.5f;
                        if(mesh->shape==int(BasicShape::Circle)||mesh->shape==int(BasicShape::Rectangle))localPoint=Vector3(0);
                        planePoint=Vector3(world*Vector4(localPoint,1));
                    }
                    shader->SetUniformFloat4("flatReflectionPlane",Vector4(planeNormal,-glm::dot(planeNormal,planePoint)));
                }
                shader->SetUniformInt("useReflection",receivesReflection);
                if(receivesReflection){
                    auto& reflection=*reflections.at(probe->GetGameObject().GetID());
                    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_CUBE_MAP,reflection.Texture());
                    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_BUFFER,reflection.GeometryTexture());
                    shader->SetUniformInt("reflectionGeometryCount",reflection.GeometryCount());
                    shader->SetUniformInt("reflectionReceiverIndex",reflection.ReceiverIndex(obj->GetID()));
                    shader->SetUniformInt("reflectionProbeIndex",reflection.ReceiverIndex(probe->GetGameObject().GetID()));
                    shader->SetUniformFloat3("reflectionCapturePosition",reflection.CapturePosition());
                    shader->SetUniformFloat("reflectionMaxLod",float(std::max(reflection.MipLevels()-1,0)));
                    if(probe->fixEdgeSeams)glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
                    else glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
                }
                auto base=MaterialSettings(material);
                auto surface=[&](uint32_t first,uint32_t count,const SurfaceMaterial& value){
                    shader->SetUniformInt("useMaterial",value.enabled);
                    shader->SetUniformInt("useReflection",receivesReflection&&value.enabled);
                    shader->SetUniformFloat("metallicity",std::clamp(value.metallicity,0.f,1.f));
                    shader->SetUniformFloat("roughness",std::clamp(value.roughness,.045f,1.f));
                    draw(mesh->color,false,value.enabled?value.albedoTexture:nullptr,count,first);
                };
                if(mesh->shape==int(BasicShape::Custom))mesh->triangleMaterials.Runs(static_cast<uint32_t>(mesh->custom.indices.size()/3),base,surface);
                else surface(0,0,base);
            }
            if(sprite && sprite->enabled)draw(sprite->color,true,sprite->texture);
        }
        if(!cubeInstances.empty())FlushCubeInstances(pendingMaterial);
        Renderer::EndScene();
        if(oldSeamless)glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);else glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    }
};
}
