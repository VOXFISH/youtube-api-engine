// Manual, deterministic five-mode depth-pass benchmark. Not a CTest timing gate.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "Scene/Components.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Rice;
namespace {
using Clock=std::chrono::steady_clock;
constexpr int Width=640,Height=360,ShadowSize=512,Warmup=3;
struct Mode {const char* name;bool enabled,culling,caching;};
constexpr Mode Modes[]={{"off",false,true,true},{"full",true,false,false},{"bvh",true,true,false},{"cache",true,false,true},{"optimized",true,true,true}};
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
double Milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double Quantile(std::vector<double> values,double q){std::sort(values.begin(),values.end());return values[std::min(values.size()-1,size_t(std::ceil(values.size()*q)-1))];}
void SetMode(SceneRenderer& renderer,const Mode& mode){
    GameObject::SetWorldCacheEnabled(true);renderer.SetSpatialCullingEnabled(true);renderer.SetCubeInstancingEnabled(true);
    // Preserve the original global-invalidation experiment after selective
    // per-light invalidation becomes the engine default.
    renderer.SetShadowSelectiveInvalidationEnabled(false);
    renderer.SetShadowsEnabled(mode.enabled);renderer.SetShadowCullingEnabled(mode.culling);renderer.SetShadowCachingEnabled(mode.caching);
}
struct Case {int count;std::string lighting,motion;bool reflection=false;
    std::string Name()const{return std::to_string(count)+"_"+lighting+"_"+motion+(reflection?"_reflection":"");}
    int LightCount()const{return lighting=="mixed8"?8:lighting=="mixed4"?4:1;}
    int Faces()const{return lighting=="directional1"?1:lighting=="point1"?6:1+(LightCount()-1)*6;}
};
struct World {
    Scene scene;
    PerspectiveCamera camera{50,float(Width)/Height,.1f,250};
    std::vector<GameObject*> objects;
    std::vector<Vector3> basePositions;
    GameObject* movingLight=nullptr;
    Vector3 lightBase{0};
    bool movingDirectional=false;
    explicit World(const Case& config){
        scene.settings.ground=true;scene.settings.directionalLight=false;
        const int side=int(std::ceil(std::sqrt(float(config.count))));
        for(int i=0;i<config.count;++i){
            auto& object=scene.CreateGameObject("caster");
            const float cell=80.f/float(side);
            object.transform.position={(float(i%side)+.5f)*cell-40.f,.65f+float(i%7)*.25f,(float(i/side)+.5f)*cell-40.f};
            object.transform.localScale={.6f+float(i%3)*.2f,1.f+float(i%5)*.15f,.6f+float(i%4)*.15f};
            object.transform.rotation.y=float((i*37)%180);
            auto& mesh=object.AddComponent<MeshRenderer>();mesh.color={.25f+float(i%5)*.12f,.35f+float(i%3)*.15f,.55f,1};
            if(i%5==0)mesh.shape=int(BasicShape::Rectangle);
            objects.push_back(&object);basePositions.push_back(object.transform.position);
        }
        for(int i=0;i<config.LightCount();++i){
            auto& object=scene.CreateGameObject("light");auto& light=object.AddComponent<Light>();
            const bool directional=config.lighting=="directional1"||(config.lighting!="point1"&&i==0);
            light.type=directional?1:0;light.direction={-.7f,-1.f,-.3f};light.range=32.f;
            light.intensity=directional?.8f:3.f;
            const float angle=float(i)*2.39996323f;
            object.transform.position=directional?Vector3(0,12,0):Vector3(22.f*std::cos(angle),10.f,22.f*std::sin(angle));
            if(!movingLight||(!directional&&movingDirectional)){movingLight=&object;movingDirectional=directional;lightBase=object.transform.position;}
        }
        if(config.reflection){
            auto& mirror=scene.CreateGameObject("reflection receiver");mirror.transform.position={0,4,0};mirror.transform.localScale={7,8,.5f};
            mirror.AddComponent<MeshRenderer>().color=Color(1);auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.15f;
            auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=64;probe.maxObjectSize=0;
        }
        Apply(config,0);
    }
    void Apply(const Case& config,int frame){
        const float phase=float(frame)*.17f;
        if(config.motion=="objects")for(size_t i=0;i<objects.size();i+=10)
            objects[i]->transform.position=basePositions[i]+Vector3(std::sin(phase+float(i)*.03f)*.75f,.15f*std::cos(phase+float(i)),0);
        if(config.motion=="lights"){
            if(movingDirectional)movingLight->GetComponent<Light>()->direction={-.7f+.2f*std::sin(phase),-1.f,-.3f};
            else movingLight->transform.position=lightBase+Vector3(3.f*std::sin(phase),0,2.f*std::cos(phase));
        }
        float angle=config.motion=="camera"?.08f*std::sin(phase):0.f;
        camera.SetPosition({78.f*std::cos(angle)+95.f*std::sin(angle),65.f,95.f*std::cos(angle)-78.f*std::sin(angle)});
        camera.LookAtObj({0,1,0});
    }
};
struct Sample {
    double cpu=0,complete=0,gpu=0;
    size_t maps=0,faces=0,candidates=0,submitted=0,draws=0,instances=0,reflectionFaces=0,boundsUpdates=0;
};
Sample Measure(SceneRenderer& renderer,ViewportTarget& target,World& world,const Case& config,int frame,GLuint query){
    glFinish();const auto start=Clock::now();world.Apply(config,frame);
    target.Render(Width,Height,[&]{glBeginQuery(GL_TIME_ELAPSED,query);renderer.Render(world.scene,world.camera);glEndQuery(GL_TIME_ELAPSED);});
    Sample sample;sample.cpu=Milliseconds(start);glFinish();sample.complete=Milliseconds(start);
    GLuint64 gpu=0;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&gpu);sample.gpu=double(gpu)/1e6;
    const auto shadow=renderer.GetShadowStats();const auto draw=renderer.GetShadowDrawStats();const auto spatial=renderer.GetSpatialStats();
    sample.maps=shadow.updates;sample.faces=shadow.faces;sample.candidates=draw.candidates;sample.submitted=draw.submitted;
    sample.draws=draw.drawCalls;sample.instances=draw.instances;sample.reflectionFaces=spatial.captureFaces;sample.boundsUpdates=spatial.boundsUpdates;
    Check(glGetError()==GL_NO_ERROR,"OpenGL error in "+config.Name());return sample;
}
std::vector<unsigned char> Capture(SceneRenderer& renderer,ViewportTarget& target,World& world){
    std::vector<unsigned char> pixels(size_t(Width)*Height*4);
    target.Render(Width,Height,[&]{renderer.Render(world.scene,world.camera);glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    Check(glGetError()==GL_NO_ERROR,"OpenGL error in parity capture");return pixels;
}
void WriteSample(std::ostream& out,const Case& config,const Mode& mode,const char* phase,int repeat,int frame,const Sample& sample){
    out<<config.Name()<<','<<config.count<<','<<config.lighting<<','<<config.LightCount()<<','<<config.motion<<','<<config.reflection<<','<<mode.name<<','<<phase<<','<<repeat<<','<<frame
       <<','<<sample.cpu<<','<<sample.complete<<','<<sample.gpu<<','<<sample.maps<<','<<sample.faces<<','<<sample.candidates<<','<<sample.submitted<<','<<sample.draws<<','<<sample.instances<<','<<sample.reflectionFaces<<','<<sample.boundsUpdates<<'\n';
}
void ValidateWork(const Case& config,const Mode& mode,const Sample& sample){
    size_t expected=0;
    if(mode.enabled){
        if(!mode.caching||config.motion=="objects")expected=size_t(config.Faces());
        else if(config.motion=="lights")expected=config.lighting=="directional1"?1:6;
    }
    Check(sample.faces==expected,"Unexpected depth-face count in "+config.Name()+"/"+mode.name+": "+std::to_string(sample.faces)+" != "+std::to_string(expected));
    Check(sample.submitted<=sample.candidates,"Submitted shadow candidates exceed exhaustive candidates");
    if(config.reflection)Check(sample.reflectionFaces==6,"Reflection probe must capture once, without multiplying shadow updates");
}
void RunCase(const Case& config,int frames,int repeats,std::ostream& raw,std::ostream& summary,std::ostream& parity){
    std::cout<<"Running "<<config.Name()<<std::endl;
    World world(config);ViewportTarget target;SceneRenderer renderer;renderer.SetShadowResolution(ShadowSize);
    GLuint query=0;glGenQueries(1,&query);
    // This first-frame sample includes lazy shader/map allocation; scene and
    // renderer construction are excluded. It is separate from warm-frame medians.
    SetMode(renderer,Modes[4]);auto cold=Measure(renderer,target,world,config,0,query);WriteSample(raw,config,Modes[4],"cold",-1,0,cold);
    world.Apply(config,5);SetMode(renderer,Modes[1]);auto reference=Capture(renderer,target,world);
    for(int modeIndex=2;modeIndex<5;++modeIndex){
        world.Apply(config,5);SetMode(renderer,Modes[modeIndex]);auto candidate=Capture(renderer,target,world);int maxError=0;size_t differences=0;
        for(size_t i=0;i<reference.size();++i){int error=std::abs(int(reference[i])-int(candidate[i]));maxError=std::max(maxError,error);if(error>1)++differences;}
        parity<<config.Name()<<','<<Modes[modeIndex].name<<','<<maxError<<','<<differences<<'\n';
        Check(differences==0,"Shadow optimization changes image in "+config.Name()+"/"+Modes[modeIndex].name);
    }
    std::vector<Sample> samples[5];
    for(int repeat=0;repeat<repeats;++repeat)for(int slot=0;slot<5;++slot){
        const int modeIndex=(repeat+slot)%5;const auto& mode=Modes[modeIndex];SetMode(renderer,mode);
        for(int frame=-Warmup;frame<frames;++frame){
            auto sample=Measure(renderer,target,world,config,frame+Warmup,query);
            if(frame>=0){ValidateWork(config,mode,sample);samples[modeIndex].push_back(sample);WriteSample(raw,config,mode,"warm",repeat,frame,sample);}
        }
    }
    glDeleteQueries(1,&query);
    for(int modeIndex=0;modeIndex<5;++modeIndex){
        const auto& values=samples[modeIndex];std::vector<double> cpu,complete,gpu;
        double maps=0,faces=0,candidates=0,submitted=0,draws=0,instances=0,reflection=0,bounds=0;
        for(const auto& sample:values){cpu.push_back(sample.cpu);complete.push_back(sample.complete);gpu.push_back(sample.gpu);maps+=sample.maps;faces+=sample.faces;candidates+=sample.candidates;submitted+=sample.submitted;draws+=sample.draws;instances+=sample.instances;reflection+=sample.reflectionFaces;bounds+=sample.boundsUpdates;}
        const double count=double(values.size());
        summary<<config.Name()<<','<<config.count<<','<<config.lighting<<','<<config.LightCount()<<','<<config.motion<<','<<config.reflection<<','<<Modes[modeIndex].name<<','<<values.size()
               <<','<<Quantile(cpu,.5)<<','<<Quantile(cpu,.95)<<','<<Quantile(complete,.5)<<','<<Quantile(complete,.95)<<','<<Quantile(gpu,.5)<<','<<Quantile(gpu,.95)
               <<','<<maps/count<<','<<faces/count<<','<<candidates/count<<','<<submitted/count<<','<<draws/count<<','<<instances/count<<','<<reflection/count<<','<<bounds/count<<'\n';
    }
    raw.flush();summary.flush();parity.flush();std::cout<<"Completed "<<config.Name()<<std::endl;
}
std::string CpuName(){
#ifdef _MSC_VER
    char* value=nullptr;size_t length=0;_dupenv_s(&value,&length,"PROCESSOR_IDENTIFIER");std::string result=value?value:"unknown";std::free(value);return result;
#else
    const char* value=std::getenv("PROCESSOR_IDENTIFIER");return value?value:"unknown";
#endif
}
}
int main(int argc,char** argv){GLFWwindow* window=nullptr;try{
    Check(argc>=2,"Usage: RiceShadowBenchmark <output-directory> [frames=20] [repeats=5] [case-substring]");
    const int frames=argc>2?std::stoi(argv[2]):20,repeats=argc>3?std::stoi(argv[3]):5;
    Check(frames>=5&&repeats>=1,"Need at least 5 measured frames and one repeat");const std::string filter=argc>4?argv[4]:"";
    std::filesystem::path output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    Debug::Init();Check(glfwInit()!=0,"GLFW init");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(Width,Height,"Shadow workload benchmark",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);glfwSwapInterval(0);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
    RenderCommand::SetDepthTestEnabled(true);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);
    std::ofstream metadata(output/"metadata.txt"),raw(output/"frames.csv"),summary(output/"summary.csv"),parity(output/"parity.csv");
    Check(bool(metadata)&&bool(raw)&&bool(summary)&&bool(parity),"Cannot open benchmark output");
    metadata<<"GPU: "<<glGetString(GL_RENDERER)<<"\nOpenGL: "<<glGetString(GL_VERSION)<<"\nCPU: "<<CpuName()<<"\nBuild: "<<__DATE__<<' '<<__TIME__
            <<"\nMain view: "<<Width<<'x'<<Height<<"\nShadow resolution: "<<ShadowSize<<"\nWarmup frames: "<<Warmup<<"\nMeasured frames per mode/run: "<<frames<<"\nRepeats: "<<repeats<<"\nFilter: "<<filter
            <<"\nModes: off; full(no depth caching/no shadow culling); bvh(culling only); cache(depth caching only); optimized(both).\n"
            <<"World-matrix and scene-spatial caches, cube instancing, and reflection culling stay enabled in all modes. No profiler session.\n"
            <<"Shadow culling-off still prepares/refits the scene BVH: culling comparisons isolate query/submission savings and retain BVH maintenance. Shadows-off may skip spatial preparation without a reflection probe; on/off includes this setup cost.\n"
            <<"Mode order per repeat: (repeat + slot) mod 5; same deterministic time sequence per mode. Every five repeats balance all five order positions; defaults give 100 samples per mode.\n"
            <<"Scene: 80x80 world footprint, deterministic grid, 80% cubes/20% rectangles, varied scales/yaw; neutral procedural ground. Directional light intensity .8; point intensity3 range32.\n"
            <<"Objects motion: 10% of casters move every frame. Lights motion: one point light (or sole directional) moves. Camera control changes no geometry/lights.\n"
            <<"Default suite: 100/1000/3000 objects x directional1/point1/mixed4(1dir3point)/mixed8(1dir7point) x static/objects/lights, plus 1000mixed4 camera. Optional static/objects reflection probe64 controls run when matched by a nonempty case filter (e.g. reflection).\n"
            <<"Reflection controls add one mirror caster/receiver and force one six-face probe capture per frame (maxObjectSize=0), keeping probe frequency equal across modes; internal captures share the outer shadow maps.\n"
            <<"CPU includes deterministic motion, viewport setup/clear, scene preparation, cache checks/BVH upkeep and GL submission. Completion waits glFinish. GPU GL_TIME_ELAPSED brackets renderer.Render (excludes viewport setup/clear), and includes GPU command starvation.\n"
            <<"One render is drained before/after each sample; these are serialized workload costs, not production pipelined FPS. Cold row is first Render per scenario (lazy GPU resources/shader), excludes scene/renderer construction.\n"
            <<"Warm p50/p95 are nearest-rank quantiles pooling all repeats. Shadow candidates count eligible caster components times rendered depth faces before BVH filtering; submitted counts surviving caster components. Counters exclude internal BVH node tests.\n"
            <<"Parity compares full shadows with bvh/cache/optimized at identical time; off intentionally differs. Max channel tolerance1.\n";
    raw<<"case,objects,lighting,lights,motion,reflection,mode,phase,repeat,frame,cpu_ms,completion_ms,gpu_ms,shadow_updates,shadow_faces,shadow_candidates,shadow_submitted,shadow_draws,shadow_instances,reflection_faces,bounds_updates\n";
    summary<<"case,objects,lighting,lights,motion,reflection,mode,samples,cpu_p50_ms,cpu_p95_ms,completion_p50_ms,completion_p95_ms,gpu_p50_ms,gpu_p95_ms,shadow_updates_mean,shadow_faces_mean,shadow_candidates_mean,shadow_submitted_mean,shadow_draws_mean,shadow_instances_mean,reflection_faces_mean,bounds_updates_mean\n";
    parity<<"case,mode,max_channel_error,channels_over_one\n";raw<<std::fixed<<std::setprecision(6);summary<<std::fixed<<std::setprecision(6);
    std::vector<Case> cases;
    for(int count:{100,1000,3000})for(const char* lighting:{"directional1","point1","mixed4","mixed8"})for(const char* motion:{"static","objects","lights"})cases.push_back({count,lighting,motion,false});
    cases.push_back({1000,"mixed4","camera",false});cases.push_back({1000,"mixed4","static",true});cases.push_back({1000,"mixed4","objects",true});
    size_t executed=0;for(const auto& config:cases)if(filter.empty()?!config.reflection:config.Name().find(filter)!=std::string::npos){RunCase(config,frames,repeats,raw,summary,parity);++executed;}
    Check(executed>0,"Filter matched no cases");metadata<<"Completed scenarios: "<<executed<<'\n';
    Check(bool(raw)&&bool(summary)&&bool(parity),"Failed writing benchmark results");glfwDestroyWindow(window);glfwTerminate();std::cout<<"Shadow benchmark complete: "<<output.string()<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
