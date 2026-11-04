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
#include "Renderer/Renderer.h"
#include "Renderer/Buffer.h"
#include "Renderer/VertexArray.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include <limits>
#include <map>
namespace Rice {
struct MeshRenderer : Component {
    Color color{0.7f,0.7f,0.8f,1};
    int shape = int(BasicShape::Cube);
    ShapeMesh custom;
    std::string shapeAsset;
    TriangleMaterials triangleMaterials;
    std::optional<std::pair<Vector3,Vector3>> GeometryBounds()const override{
        if(shape==int(BasicShape::Custom))return custom.Bounds();
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
    Color color{1}; float intensity=1; Vector3 direction{-1,-1,-1};
    int type=0; // 0: Point, 1: Directional
    float range=10;
    Vector3 WorldPosition()const{ RICE_BENCHMARK_FUNCTION(); return Vector3(GetGameObject().GetWorldMatrix()[3]);}
    Vector3 WorldDirection()const{ RICE_BENCHMARK_FUNCTION(); auto value=GetGameObject().GetWorldRotation()*direction;return glm::length(value)>.0001f?glm::normalize(value):Vector3(0,-1,0);}
};
struct BoxCollider : Component {
    Vector3 center{0}, size{1}; bool overlapping = false, isTrigger = false;
    std::pair<Vector3,Vector3> GetBounds() const {
        RICE_BENCHMARK_FUNCTION();
        Vector3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
        auto matrix = GetGameObject().GetWorldMatrix();
        for (int i=0;i<8;++i) {
            Vector3 corner = center + glm::abs(size)*Vector3(i&1 ? .5f:-.5f,i&2 ? .5f:-.5f,i&4 ? .5f:-.5f);
            Vector3 world(matrix*Vector4(corner,1)); low=glm::min(low,world); high=glm::max(high,world);
        }
        return {low,high};
    }
};
class CollisionSystem {
public:
    static size_t Update(Scene& scene) {
        RICE_BENCHMARK_FUNCTION();
        std::vector<BoxCollider*> colliders;
        for (auto& obj:scene.GetObjects()) if (auto* c=obj->GetComponent<BoxCollider>()) {
            c->overlapping=false; if(obj->IsActive() && c->enabled) colliders.push_back(c);
        }
        size_t count=0;
        for(size_t i=0;i<colliders.size();++i) for(size_t j=i+1;j<colliders.size();++j) {
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
    struct RenderStats {
        uint32_t drawCalls=0,instancedDrawCalls=0,instancedInstances=0;
    };
    void SetCubeInstancingEnabled(bool enabled) { RICE_BENCHMARK_FUNCTION(); cubeInstancingEnabled=enabled; }
    const RenderStats& GetRenderStats() const { RICE_BENCHMARK_FUNCTION(); return renderStats; }
private:
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
)" )+ReflectionIntersectionGLSL+R"(
uniform samplerCube environmentMap;
uniform float reflectionMaxLod;
uniform int cubeLayout;
in vec3 localNormal;
in vec3 localPosition;
vec2 materialUV(){
    if(cubeLayout==0)return UV;
    vec3 n=localNormal;
    int face;
    if(abs(n.x)>=abs(n.y) && abs(n.x)>=abs(n.z))face=n.x>0.0?0:1;
    else if(abs(n.y)>=abs(n.z))face=n.y>0.0?2:3;
    else face=n.z>0.0?4:5;
    vec2 cell;
    vec2 grid;
    vec2 uv=UV;
    if(cubeLayout==1){
        grid=vec2(3,4);
        if(face==0)cell=vec2(1,1);
        if(face==1){cell=vec2(1,3);uv=vec2(-localPosition.z,-localPosition.y)+.5;}
        if(face==2){cell=vec2(1,0);uv=vec2(-localPosition.z,-localPosition.x)+.5;}
        if(face==3){cell=vec2(1,2);uv=vec2(-localPosition.z,localPosition.x)+.5;}
        if(face==4)cell=vec2(0,1);
        if(face==5)cell=vec2(2,1);
    }else if(cubeLayout==2){
        grid=vec2(4,3);
        if(face==0)cell=vec2(2,1);
        if(face==1)cell=vec2(0,1);
        if(face==2)cell=vec2(1,0);
        if(face==3)cell=vec2(1,2);
        if(face==4)cell=vec2(1,1);
        if(face==5)cell=vec2(3,1);
    }else{
        grid=cubeLayout==3?vec2(6,1):vec2(1,6);
        cell=cubeLayout==3?vec2(face,0):vec2(0,face);
    }
    // Source cells use top-left image coordinates. The loader flips images for GL.
    cell.y=grid.y-1.0-cell.y;
    vec2 pixels=vec2(textureSize(image,0))/grid;
    uv=clamp(uv,0.0,1.0)*(1.0-1.0/pixels)+.5/pixels;
    return (cell+uv)/grid;
}
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
    light+=response*lightColor[i]*attenuation*nl;
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
        cubeInstances.clear();
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT,viewport);
        struct Receiver { ReflectionProbe* probe;float size; };
        std::map<EntityID,Receiver> receivers;
        std::vector<ReflectionProbe*> probes;
        if(!capturing){
            if(reflectionScene!=scene.GetInstanceID()){
                reflections.clear();
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
                if(mesh->shape==int(BasicShape::Custom))std::tie(low,high)=mesh->custom.Bounds();
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
                bool needsUpdate=reflection->SettingsChanged(*probe);
                for(auto& [objectID,receiver]:receivers)
                    if(receiver.probe==probe && receiver.size>0 && receiver.size>=std::max(probe->maxObjectSize,0.f))needsUpdate=true;
                if(!needsUpdate)continue;
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
                    Render(scene,captureCamera,true,id);
                });
            }
        }
        environment.Draw(scene,camera,quad,editorGuides&&!capturing);
        // Recursive reflection captures finish first; expose this view's submissions only.
        renderStats={};
        bool oldSeamless=glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        Renderer::BeginScene(camera); shader->Bind(); int count=0;
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
        for(auto& obj:scene.GetObjects()) if(obj->GetID()!=excluded && obj->IsActive() && glm::all(glm::greaterThan(glm::abs(obj->transform.localScale),Vector3(.00001f)))) {
            auto* mesh=obj->GetComponent<MeshRenderer>(); auto* sprite=obj->GetComponent<SpriteRenderer>();
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
                    cubeInstances.push_back({obj->GetWorldMatrix(),mesh->color});
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
                if(texture)texture->Bind(0); Renderer::Submit(shader,isSprite?quad:mesh->shape==int(BasicShape::Custom)?CustomArray(*mesh):shapes[std::clamp(mesh->shape,0,5)],obj->GetWorldMatrix(),indexCount,firstIndex);
                ++renderStats.drawCalls;
            };
            if(mesh && mesh->enabled){
                auto* material=obj->GetComponent<Material>();
                bool active=(material && material->enabled)||(mesh->shape==int(BasicShape::Custom)&&mesh->triangleMaterials.HasEnabled());
                shader->SetUniformInt("useMaterial",active);
                auto world=obj->GetWorldMatrix();
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
