// Manual A/B benchmark, excluded from CTest. Reports complete workloads including
// scene-cache validation/BVH maintenance, not just traversal microbenchmarks.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "Scene/Components.h"
#include "Scene/Gameplay.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
using namespace Rice;
namespace {
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double Median(std::vector<double> samples){std::sort(samples.begin(),samples.end());return samples[samples.size()/2];}
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Mode {const char* name;bool cache,bvh;};
constexpr Mode Modes[]={{"legacy",false,false},{"cache_only",true,false},{"cache_bvh",true,true}};
void SetMode(SceneRenderer* renderer,Mode mode){GameObject::SetWorldCacheEnabled(mode.cache);if(renderer)renderer->SetSpatialCullingEnabled(mode.bvh);}
std::vector<bool> Flags(const std::vector<GameObject*>& objects){std::vector<bool> result;for(auto* object:objects)result.push_back(object->GetComponent<BoxCollider>()->overlapping);return result;}
void Collisions(int count,bool moving,bool dense=false){
    Scene scene;std::vector<GameObject*> objects;std::vector<Vector3> positions;
    for(int i=0;i<count;++i){auto& object=scene.CreateGameObject();object.transform.position={float(i%20)*2,float((i/20)%20)*2,float(i/400)*2};if(dense)object.transform.position*=.005f;auto& collider=object.AddComponent<BoxCollider>();collider.size=Vector3(i%7==0?2.2f:.8f);objects.push_back(&object);positions.push_back(object.transform.position);}
    auto move=[&](int frame){if(moving)for(size_t i=0;i<objects.size();++i)objects[i]->transform.position=positions[i]+Vector3(.7f*std::sin(float(frame)*.17f+float(i)),0,0);};
    std::vector<double> timings[3];size_t comparisons[3]{},overlaps[3]{};
    for(int repeat=0;repeat<3;++repeat)for(int slot=0;slot<3;++slot){int modeIndex=repeat%2?2-slot:slot;auto mode=Modes[modeIndex];SetMode(nullptr,mode);
        for(int frame=-1;frame<4;++frame){auto start=Clock::now();move(frame+1);size_t checks=0;auto result=CollisionSystem::Update(scene,mode.bvh,&checks);double time=Ms(start);if(frame>=0){timings[modeIndex].push_back(time);comparisons[modeIndex]=checks;overlaps[modeIndex]=result;}}
    }
    move(3);SetMode(nullptr,Modes[0]);auto expected=CollisionSystem::Update(scene,false);auto flags=Flags(objects);SetMode(nullptr,Modes[2]);Check(expected==CollisionSystem::Update(scene,true)&&flags==Flags(objects),"Benchmark collision results differ");
    for(int i=0;i<3;++i)std::cout<<"collision,"<<count<<','<<(dense?"dense":moving?"moving":"static")<<','<<Modes[i].name<<','<<Median(timings[i])<<','<<Median(timings[i])<<','<<comparisons[i]<<','<<overlaps[i]<<",0,0,0\n";
}
void Physics(bool dense){
    const int count=dense?64:1000;std::vector<double> timings[3];PhysicsSystem::BroadphaseStats stats[3];std::vector<Vector3> expected;
    for(int repeat=0;repeat<3;++repeat)for(int slot=0;slot<3;++slot){int modeIndex=repeat%2?2-slot:slot;auto mode=Modes[modeIndex];SetMode(nullptr,mode);
        Scene scene;PhysicsSystem physics;physics.SetBroadphaseEnabled(mode.bvh);std::vector<GameObject*> objects;
        for(int i=0;i<count;++i){auto& object=scene.CreateGameObject();object.transform.position=dense?Vector3(float(i%4)*.1f,float((i/4)%4)*.1f,float(i/16)*.1f):Vector3(float(i%20)*4,float((i/20)%20)*4,float(i/400)*4);object.AddComponent<BoxCollider>();auto& body=object.AddComponent<Rigidbody>();body.useGravity=false;body.velocity={.1f,0,0};objects.push_back(&object);}
        auto start=Clock::now();physics.Update(scene,1.f/60.f);timings[modeIndex].push_back(Ms(start));stats[modeIndex]=physics.GetBroadphaseStats();
        std::vector<Vector3> actual;for(auto* object:objects)actual.push_back(object->transform.position);
        if(expected.empty())expected=actual;else for(size_t i=0;i<actual.size();++i)Check(glm::length(actual[i]-expected[i])<1e-5f,"Benchmark physics resolution differs");
    }
    for(int i=0;i<3;++i)std::cout<<"physics,"<<count<<','<<(dense?"dense":"sparse")<<','<<Modes[i].name<<','<<Median(timings[i])<<','<<Median(timings[i])<<','<<stats[i].possiblePairs<<','<<stats[i].candidatePairs<<",0,"<<stats[i].boundsRefreshes<<','<<(stats[i].treeBuilds+stats[i].treeRefits)<<'\n';
}
void Render(bool moving){
    constexpr int count=1000,width=64,height=64;
    Scene scene;scene.settings.directionalLight=true;
    auto& mirror=scene.CreateGameObject("benchmark mirror");mirror.transform.localScale=Vector3(2);mirror.AddComponent<MeshRenderer>().color=Color(1);auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.2f;auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=64;probe.maxObjectSize=0;
    std::vector<GameObject*> objects;std::vector<Vector3> positions;
    // Deterministic volumetric distribution surrounding the reflection probe.
    for(int i=0;i<count;++i){auto& object=scene.CreateGameObject();float angle=float(i)*2.39996323f;float y=1.f-2.f*(float(i)+.5f)/float(count);float r=std::sqrt(1-y*y);float distance=20.f+float(i%11)*6.f;object.transform.position=distance*Vector3(r*std::cos(angle),y,r*std::sin(angle));auto& mesh=object.AddComponent<MeshRenderer>();mesh.color={.25f+float(i%5)*.15f,.3f+float(i%3)*.2f,.6f,1};if(i%10==0)mesh.shape=int(BasicShape::Rectangle);objects.push_back(&object);positions.push_back(object.transform.position);}
    SceneRenderer renderer;ViewportTarget target;PerspectiveCamera camera(55,1,.1f,150);camera.SetPosition({0,0,6});camera.LookAtObj({0,0,0});RenderCommand::SetDepthTestEnabled(true);
    auto move=[&](int frame){if(moving)for(size_t i=0;i<objects.size();++i)objects[i]->transform.position=positions[i]+Vector3(std::sin(float(frame)*.17f+float(i))*.5f,0,0);};
    auto image=[&](Mode mode){SetMode(&renderer,mode);std::vector<unsigned char> pixels(width*height*4);target.Render(width,height,[&]{renderer.Render(scene,camera);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});return pixels;};
    Check(image(Modes[0])==image(Modes[2]),"Benchmark reflection image differs before timing");
    std::vector<double> cpu[3],complete[3];SceneRenderer::SpatialStats stats[3];
    for(int repeat=0;repeat<3;++repeat)for(int slot=0;slot<3;++slot){int modeIndex=repeat%2?2-slot:slot;SetMode(&renderer,Modes[modeIndex]);
        for(int frame=-3;frame<10;++frame){glFinish();auto start=Clock::now();move(frame+3);target.Render(width,height,[&]{renderer.Render(scene,camera);});double submission=Ms(start);glFinish();double finished=Ms(start);if(frame>=0){cpu[modeIndex].push_back(submission);complete[modeIndex].push_back(finished);stats[modeIndex]=renderer.GetSpatialStats();}}
    }
    Check(image(Modes[0])==image(Modes[2]),"Benchmark reflection image differs after timing");
    for(int i=0;i<3;++i)std::cout<<"cubemap,"<<count<<','<<(moving?"moving":"static")<<','<<Modes[i].name<<','<<Median(cpu[i])<<','<<Median(complete[i])<<','<<stats[i].captureCandidates<<','<<stats[i].captureSubmitted<<','<<stats[i].captureFaces<<','<<stats[i].boundsUpdates<<','<<(stats[i].builds+stats[i].refits)<<'\n';
    Check(glGetError()==GL_NO_ERROR,"OpenGL error during spatial benchmark");
}
}
int main(){GLFWwindow* window=nullptr;try{
    Debug::Init();Check(glfwInit()!=0,"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(64,64,"Spatial benchmark",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);glfwSwapInterval(0);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
    std::cout<<"# GPU: "<<glGetString(GL_RENDERER)<<"\n# OpenGL: "<<glGetString(GL_VERSION)<<"\n# CPU: "<<(std::getenv("PROCESSOR_IDENTIFIER")?std::getenv("PROCESSOR_IDENTIFIER"):"unknown")<<"\n# Release build, profiler inactive. 3 alternating mode-order runs; medians across all measured frames.\n# Render: 3 warmup + 10 measured frames/run, 64x64 main view, 64x64 x 6 cubemap, 1000 sources plus one receiver. Collision: 1 warmup + 4 measured updates/run.\n# Physics: median of 3 fresh-scene initial steps, including BVH build and sequential resolution; excludes scene construction.\n# CPU includes direct movement, cache validation, BVH upkeep, submission; completion additionally waits glFinish. No editor UI/swap; not FPS.\n# candidates=all-pair checks or pre-cull capture objects; submitted=overlap results or capture submissions. Physics reports possible/candidate pairs. Tree-internal bound tests are not counted.\n";
    std::cout<<std::fixed<<std::setprecision(6)<<"workload,objects,motion,mode,cpu_median_ms,completion_median_ms,candidates,submitted,capture_faces,bounds_updates,tree_updates\n";
    for(int count:{100,1000})for(bool moving:{false,true})Collisions(count,moving);
    Collisions(64,false,true);Physics(false);Physics(true);
    Render(false);Render(true);GameObject::SetWorldCacheEnabled(true);glfwDestroyWindow(window);glfwTerminate();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
