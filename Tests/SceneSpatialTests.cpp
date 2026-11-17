#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "Scene/Components.h"
#include "Scene/SceneSpatial.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <iostream>
#include <random>
#include <set>
using namespace Rice;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(const Matrix4& a,const Matrix4& b){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>1e-5f)return false;return true;}
void CheckWorld(GameObject& object){
    GameObject::SetWorldCacheEnabled(false);auto expected=object.GetWorldMatrix();
    GameObject::SetWorldCacheEnabled(true);Check(Near(expected,object.GetWorldMatrix()),"Cached world matrix differs after edit");
    Check(Near(expected,object.GetWorldMatrix()),"Repeated cached world matrix differs");
}
bool ClipVisible(const SpatialBounds& box,const Matrix4& vp){
    Vector4 corners[8];for(int i=0;i<8;++i)corners[i]=vp*Vector4(i&1?box.high.x:box.low.x,i&2?box.high.y:box.low.y,i&4?box.high.z:box.low.z,1);
    for(int axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}){
        bool outside=true;for(auto corner:corners)if(corner.w+sign*corner[axis]>=0)outside=false;
        if(outside)return false;
    }return true;
}
void CheckBVH(){
    std::mt19937 random(731);std::uniform_real_distribution<float> position(-50,50),extent(.01f,4);
    std::vector<SpatialBounds> bounds;
    for(int i=0;i<250;++i){Vector3 center(position(random),position(random),position(random));Vector3 size(extent(random),extent(random),extent(random));bounds.push_back({center-size,center+size});}
    SpatialBVH tree;tree.Build(bounds);
    for(int iteration=0;iteration<2;++iteration){
        if(iteration){for(auto& box:bounds){box.low.x+=17;box.high.x+=17;}tree.Refit(bounds);}
        for(int i=0;i<80;++i){
            Vector3 center(position(random),position(random),position(random));SpatialBounds query{center-Vector3(8),center+Vector3(8)};
            std::set<size_t> expected,actual;
            for(size_t j=0;j<bounds.size();++j)if(glm::all(glm::lessThanEqual(bounds[j].low,query.high))&&glm::all(glm::lessThanEqual(query.low,bounds[j].high)))expected.insert(j);
            tree.QueryOverlaps(query,[&](size_t j){actual.insert(j);});Check(expected==actual,"BVH overlap query differs from exhaustive scan");
        }
        for(int i=0;i<12;++i){
            Vector3 eye(position(random),position(random),position(random));auto vp=glm::perspective(glm::radians(90.f),1.f,.05f,100.f)*glm::lookAt(eye,Vector3(0),Vector3(0,1,0));
            std::set<size_t> actual;tree.QueryFrustum(vp,[&](size_t j){actual.insert(j);});
            for(size_t j=0;j<bounds.size();++j)if(ClipVisible(bounds[j],vp))Check(actual.contains(j),"BVH frustum query culled a visible box");
        }
    }
    tree.Build({});size_t emptyHits=0;tree.QueryOverlaps({Vector3(-1),Vector3(1)},[&](size_t){++emptyHits;});Check(!emptyHits,"Empty BVH returns objects");
    tree.Build({{Vector3(0),Vector3(1)}});size_t touching=0;tree.QueryOverlaps({Vector3(1),Vector3(2)},[&](size_t){++touching;});Check(touching==1,"Touching AABBs remain collision candidates");
}
void CheckCaches(){
    Scene scene;auto& parent=scene.CreateGameObject("parent");auto& group=scene.CreateGameObject("group");group.isGroup=true;
    auto& object=scene.CreateGameObject("object");auto& mesh=object.AddComponent<MeshRenderer>();object.SetParent(&parent);
    CheckWorld(object);object.transform.position={2,3,4};CheckWorld(object);parent.transform.rotation={13,25,47};CheckWorld(object);
    object.transform.localScale={-2,.3f,4};object.transform.pivot={.1f,.8f,.2f};CheckWorld(object);
    object.SetGroup(&group);group.transform.position={3,-2,1};CheckWorld(object);group.transform.rotation.y=40;CheckWorld(object);
    object.groupOffset[3].z+=3;CheckWorld(object);
    auto& anchor=parent.AddComponent<Anchor>();anchor.child=object.GetID();CheckWorld(object);anchor.transform.position={4,2,-1};CheckWorld(object);anchor.transform.rotation.z=31;CheckWorld(object);
    anchor.enabled=false;CheckWorld(object);object.SetGroup(nullptr);object.SetParent(nullptr);CheckWorld(object);
    mesh.SetShape(int(BasicShape::Custom));mesh.custom.positions[0].x-=3;mesh.custom.Touch();CheckWorld(object);
    SceneSpatialCache cache;
    auto bounds=[](const GameObject& candidate)->std::optional<std::pair<Vector3,Vector3>>{auto* m=candidate.GetComponent<MeshRenderer>();return m&&m->enabled?m->GeometryBounds():std::nullopt;};
    cache.Prepare(scene,bounds);Check(cache.Entries().size()==1,"Spatial cache collects active renderable");
    cache.Prepare(scene,bounds);Check(cache.GetStats().boundsUpdates==0&&cache.GetStats().builds==0&&cache.GetStats().refits==0,"Unchanged snapshot rebuilds bounds or BVH");
    auto first=cache.Entries()[0].bounds;object.transform.position.x+=20;cache.Prepare(scene,bounds);
    Check(std::abs(cache.Entries()[0].bounds.low.x-first.low.x-20)<1e-4f,"Spatial cache follows direct transform edits");
    mesh.custom.positions[0].z+=12;mesh.custom.Touch();cache.Prepare(scene,bounds);
    auto expected=SpatialBounds::Transform(object.GetWorldMatrix(),{mesh.custom.Bounds().first,mesh.custom.Bounds().second});
    Check(glm::length(cache.Entries()[0].bounds.high-expected.high)<1e-4f,"Spatial cache follows custom geometry edits");
    mesh.enabled=false;cache.Prepare(scene,bounds);Check(cache.Entries().empty(),"Disabled mesh leaves scene BVH");mesh.enabled=true;
    object.activeSelf=false;cache.Prepare(scene,bounds);Check(cache.Entries().empty(),"Inactive object leaves scene BVH");object.activeSelf=true;
    scene.Destroy(object.GetID());cache.Prepare(scene,bounds);Check(cache.Entries().empty(),"Pending deletion leaves scene BVH");
    Scene replacement;auto& another=replacement.CreateGameObject("replacement");another.AddComponent<MeshRenderer>();cache.Prepare(replacement,bounds);Check(cache.Entries().size()==1&&cache.Entries()[0].object==&another,"Cache resets for another scene");
}
void CheckCollisions(){
    Scene scene;std::vector<GameObject*> objects;
    for(int i=0;i<180;++i){auto& o=scene.CreateGameObject();o.transform.position={float(i%15)*1.2f,float(i/15)*1.2f,0};o.AddComponent<BoxCollider>().size=Vector3(i%4==0?1.4f:.8f);objects.push_back(&o);}
    auto compare=[&]{
        size_t oldChecks=0,newChecks=0;
        GameObject::SetWorldCacheEnabled(false);size_t oldCount=CollisionSystem::Update(scene,false,&oldChecks);std::vector<bool> flags;for(auto* o:objects)flags.push_back(o->GetComponent<BoxCollider>()->overlapping);
        GameObject::SetWorldCacheEnabled(true);size_t newCount=CollisionSystem::Update(scene,true,&newChecks);Check(oldCount==newCount,"Collision BVH overlap count differs");
        for(size_t i=0;i<objects.size();++i)Check(flags[i]==objects[i]->GetComponent<BoxCollider>()->overlapping,"Collision BVH overlap flags differ");
        Check(newChecks<oldChecks,"Sparse collision BVH fails to reduce comparisons");
    };
    compare();objects[3]->transform.position={0,0,0};objects[4]->GetComponent<BoxCollider>()->center={-4,1,0};objects[5]->transform.localScale={-2,1,1};compare();
    objects[0]->SetActive(false);objects[1]->GetComponent<BoxCollider>()->enabled=false;objects[2]->SetPoolState(true,true);compare();
}
void CheckRendering(){
    Scene scene;auto& mirror=scene.CreateGameObject("mirror");mirror.AddComponent<MeshRenderer>().color=Color(1);auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.05f;
    auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=32;probe.maxObjectSize=0;
    const Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(int face=0;face<6;++face){auto& o=scene.CreateGameObject("source");o.transform.position=directions[face]*9.f;o.transform.localScale={3,3,3};o.AddComponent<MeshRenderer>().color={float(face%3==0),float(face%3==1),float(face%3==2),1};}
    auto& parent=scene.CreateGameObject("moving parent");auto& edge=scene.CreateGameObject("cube face boundary");edge.SetParent(&parent);edge.transform.position={5,0,5};edge.transform.localScale={3,2,3};edge.AddComponent<MeshRenderer>().color={1,.5f,.2f,1};
    auto& flat=scene.CreateGameObject("sprite");flat.transform.position={-5,0,5};flat.AddComponent<SpriteRenderer>().color={.2f,.8f,.2f,1};
    auto& custom=scene.CreateGameObject("custom");custom.transform.position={0,5,5};custom.transform.localScale={-2,2,2};custom.AddComponent<MeshRenderer>().SetShape(int(BasicShape::Custom));
    SceneRenderer renderer;ViewportTarget target;PerspectiveCamera camera(55,1,.1f,100);RenderCommand::SetDepthTestEnabled(true);
    auto image=[&](bool enabled){renderer.SetSpatialCullingEnabled(enabled);GameObject::SetWorldCacheEnabled(enabled);std::vector<unsigned char> pixels(96*96*4);target.Render(96,96,[&]{renderer.Render(scene,camera);glReadPixels(0,0,96,96,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});return pixels;};
    for(int frame=0;frame<12;++frame){
        int face=frame%6;camera.SetPosition(directions[face]*5.f);camera.LookAtObj(face==2||face==3?Vector3(.001f,0,0):Vector3(0));
        if(frame==6){parent.transform.rotation.y=15;parent.transform.position.z=1;custom.GetComponent<MeshRenderer>()->custom.positions[0].x-=2;custom.GetComponent<MeshRenderer>()->custom.Touch();}
        auto reference=image(false),optimized=image(true);Check(reference==optimized,"Cubemap frustum culling changes reflected image");
    }
    SceneRenderer directRenderer;
    auto directImage=[&](bool enabled){directRenderer.SetSpatialCullingEnabled(enabled);std::vector<unsigned char> pixels(96*96*4);target.Render(96,96,[&]{directRenderer.Render(scene,camera,true);glReadPixels(0,0,96,96,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});return pixels;};
    auto directOptimized=directImage(true);Check(directOptimized==directImage(false),"First direct capture call lacks a prepared scene snapshot");
    GameObject::SetWorldCacheEnabled(true);Check(glGetError()==GL_NO_ERROR,"Spatial rendering leaves OpenGL errors");
}
}
int main(){GLFWwindow* window=nullptr;try{
    Debug::Init();CheckBVH();CheckCaches();CheckCollisions();
    Check(glfwInit()!=0,"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(96,96,"Spatial regression",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");CheckRendering();
    glfwDestroyWindow(window);glfwTerminate();std::cout<<"Scene spatial regression passed (BVH, cache edits, collisions, 12 exact reflection images)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
