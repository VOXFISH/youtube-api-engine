// Manual old/global versus selective shadow-cache invalidation benchmark.
// Timing is not a CTest gate. Each mode owns independent scene/cache history.
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Rice;
namespace {
using Clock=std::chrono::steady_clock;
constexpr int Width=640,Height=360,ShadowSize=512,Warmup=3;
constexpr const char* Modes[]={"global","selective"};
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
double Ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double Quantile(std::vector<double> values,double q){std::sort(values.begin(),values.end());return values[std::min(values.size()-1,size_t(std::ceil(values.size()*q)-1))];}
struct Case {
    int objects;std::string lighting,motion;bool reflection=false;
    std::string Name()const{return std::to_string(objects)+"_"+lighting+"_"+motion+(reflection?"_reflection":"");}
    int Points()const{return lighting=="directional1"?0:lighting=="point4"?4:lighting=="mixed8"?7:8;}
    int Directionals()const{return lighting=="directional1"||lighting=="mixed8"?1:0;}
    int Lights()const{return Points()+Directionals();}
    int Clusters()const{return lighting=="overlap8"?1:std::max(Points(),1);}
    int Faces()const{return Points()*6+Directionals();}
    int ExpectedFaces(bool selective)const{
        if(motion=="static")return 0;
        if(!selective)return Faces();
        if(motion=="outside")return Directionals();
        if(motion=="distributed"||lighting=="overlap8")return Faces();
        return (Points()?6:0)+Directionals();
    }
    int ExpectedUpdates(bool selective)const{
        if(motion=="static")return 0;
        if(!selective)return Lights();
        if(motion=="outside")return Directionals();
        if(motion=="distributed"||lighting=="overlap8")return Lights();
        return (Points()?1:0)+Directionals();
    }
};
Vector3 ClusterCenter(int cluster,int count){
    const int columns=count<=4?2:4,rows=(count+columns-1)/columns;
    if(count==1)return {0,0,0};
    return {(float(cluster%columns)-float(columns-1)*.5f)*52.f,0,(float(cluster/columns)-float(rows-1)*.5f)*52.f};
}
struct World {
    Scene scene;
    PerspectiveCamera camera{55,float(Width)/Height,.1f,600};
    std::vector<GameObject*> objects;
    std::vector<Vector3> positions;
    explicit World(const Case& config){
        scene.settings.ground=true;scene.settings.directionalLight=false;
        const int clusters=config.Clusters(),perCluster=(config.objects+clusters-1)/clusters,side=int(std::ceil(std::sqrt(float(perCluster))));
        for(int i=0;i<config.objects;++i){
            auto& object=scene.CreateGameObject("caster");
            const int cluster=i*clusters/config.objects,start=(cluster*config.objects+clusters-1)/clusters,local=i-start;
            const float cell=16.f/float(side);
            object.transform.position=ClusterCenter(cluster,clusters)+Vector3((float(local%side)+.5f)*cell-8.f,.8f+float(i%7)*.25f,(float(local/side)+.5f)*cell-8.f);
            if(config.motion=="outside"&&i<config.objects/10)object.transform.position.x+=250.f;
            object.transform.localScale={.6f+float(i%3)*.2f,1.f+float(i%5)*.15f,.6f+float(i%4)*.15f};
            object.transform.rotation.y=float((i*37)%180);
            auto& mesh=object.AddComponent<MeshRenderer>();mesh.color={.25f+float(i%5)*.12f,.35f+float(i%3)*.15f,.55f,1};
            if(i%5==0)mesh.shape=int(BasicShape::Rectangle);
            objects.push_back(&object);positions.push_back(object.transform.position);
        }
        if(config.Directionals()){
            auto& object=scene.CreateGameObject("directional light");auto& light=object.AddComponent<Light>();
            light.type=1;light.direction={-.7f,-1.f,-.3f};light.intensity=.8f;
        }
        for(int i=0;i<config.Points();++i){
            auto& object=scene.CreateGameObject("point light");object.transform.position=ClusterCenter(config.lighting=="overlap8"?0:i,clusters)+Vector3(0,12,0);
            auto& light=object.AddComponent<Light>();light.range=20;light.intensity=config.lighting=="overlap8"?.4f:3.f;
        }
        if(config.reflection){
            auto& mirror=scene.CreateGameObject("reflection receiver");mirror.transform.position=ClusterCenter(0,clusters)+Vector3(0,4,4);mirror.transform.localScale={8,8,.5f};
            mirror.AddComponent<MeshRenderer>().color=Color(1);auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.15f;
            auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=64;probe.maxObjectSize=0;
        }
        const float viewExtent=clusters==1?22.f:clusters<=4?65.f:115.f;
        camera.SetPosition({viewExtent*.35f,viewExtent*.9f,viewExtent*1.3f});camera.LookAtObj({0,1,0});Apply(config,0);
    }
    void Apply(const Case& config,int frame){
        if(config.motion=="static")return;
        const float phase=float(frame)*.17f;
        for(size_t i=0;i<objects.size();++i){
            const bool moving=config.motion=="distributed"?i%10==0:i<size_t(config.objects/10);
            if(moving)objects[i]->transform.position=positions[i]+Vector3(.6f*std::sin(phase+float(i)*.03f),.15f*std::cos(phase+float(i)),0);
        }
    }
};
struct Rig {
    World world;SceneRenderer renderer;ViewportTarget target;
    Rig(const Case& config,bool selective,bool caching=true):world(config){
        renderer.SetShadowsEnabled(true);renderer.SetShadowResolution(ShadowSize);renderer.SetShadowCullingEnabled(true);
        renderer.SetShadowCachingEnabled(caching);renderer.SetShadowSelectiveInvalidationEnabled(selective);
        renderer.SetSpatialCullingEnabled(true);renderer.SetCubeInstancingEnabled(true);
    }
};
struct Sample {
    double cpu=0,complete=0,gpu=0;
    size_t updates=0,faces=0,candidates=0,submitted=0,draws=0,instances=0,reflectionFaces=0,boundsUpdates=0;
    size_t changedCasters=0,changedBounds=0,affectedLights=0,dirtyBVHBuilds=0;
};
Sample Stats(const SceneRenderer& renderer){
    Sample sample;const auto& depth=renderer.GetShadowStats();const auto& draw=renderer.GetShadowDrawStats();const auto& spatial=renderer.GetSpatialStats();
    const auto& invalidation=renderer.GetShadowInvalidationStats();
    sample.updates=depth.updates;sample.faces=depth.faces;sample.candidates=draw.candidates;sample.submitted=draw.submitted;
    sample.draws=draw.drawCalls;sample.instances=draw.instances;sample.reflectionFaces=spatial.captureFaces;sample.boundsUpdates=spatial.boundsUpdates;
    sample.changedCasters=invalidation.changedCasters;sample.changedBounds=invalidation.changedBounds;sample.affectedLights=invalidation.affectedLights;sample.dirtyBVHBuilds=invalidation.bvhBuilds;return sample;
}
void Validate(const Case& config,const Sample& sample,bool selective,bool first=false){
    const size_t faces=size_t(first?config.Faces():config.ExpectedFaces(selective)),updates=size_t(first?config.Lights():config.ExpectedUpdates(selective));
    Check(sample.faces==faces&&sample.updates==updates,"Unexpected shadow invalidation count in "+config.Name()+"/"+Modes[selective]+": faces="+std::to_string(sample.faces)+", expected="+std::to_string(faces));
    Check(sample.submitted<=sample.candidates,"Submitted caster count exceeds potential candidates");
    Check(sample.reflectionFaces==size_t(config.reflection?6:0),"Reflection capture count changed between modes");
    if(selective&&!first)Check(sample.changedCasters==size_t(config.motion=="static"?0:config.objects/10),"Changed caster count differs from deterministic motion");
}
Sample Measure(Rig& rig,const Case& config,int frame,GLuint query){
    glFinish();auto start=Clock::now();rig.world.Apply(config,frame);
    rig.target.Render(Width,Height,[&]{glBeginQuery(GL_TIME_ELAPSED,query);rig.renderer.Render(rig.world.scene,rig.world.camera);glEndQuery(GL_TIME_ELAPSED);});
    double cpu=Ms(start);glFinish();double complete=Ms(start);GLuint64 gpu=0;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&gpu);
    auto sample=Stats(rig.renderer);sample.cpu=cpu;sample.complete=complete;sample.gpu=double(gpu)/1e6;
    Check(glGetError()==GL_NO_ERROR,"OpenGL error in "+config.Name());return sample;
}
std::vector<unsigned char> Capture(Rig& rig,const Case& config,int frame){
    rig.world.Apply(config,frame);std::vector<unsigned char> pixels(size_t(Width)*Height*4);
    rig.target.Render(Width,Height,[&]{rig.renderer.Render(rig.world.scene,rig.world.camera);glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    Check(glGetError()==GL_NO_ERROR,"OpenGL error during parity capture");return pixels;
}
void WriteSample(std::ostream& out,const Case& config,int mode,const char* phase,int repeat,int frame,const Sample& sample){
    out<<config.Name()<<','<<config.objects<<','<<config.lighting<<','<<config.Lights()<<','<<config.motion<<','<<config.reflection<<','<<Modes[mode]<<','<<phase<<','<<repeat<<','<<frame
       <<','<<sample.cpu<<','<<sample.complete<<','<<sample.gpu<<','<<sample.updates<<','<<sample.faces<<','<<sample.candidates<<','<<sample.submitted<<','<<sample.draws<<','<<sample.instances<<','<<sample.reflectionFaces<<','<<sample.boundsUpdates
       <<','<<sample.changedCasters<<','<<sample.changedBounds<<','<<sample.affectedLights<<','<<sample.dirtyBVHBuilds<<'\n';
}
void Run(const Case& config,int frames,int repeats,std::ostream& raw,std::ostream& summary,std::ostream& parity){
    std::cout<<"Running "<<config.Name()<<std::endl;
    auto global=std::make_unique<Rig>(config,false),selective=std::make_unique<Rig>(config,true);
    Rig* rigs[]={global.get(),selective.get()};GLuint query=0;glGenQueries(1,&query);
    // Independently allocated rigs avoid cross-mode cache priming. First-render
    // allocation costs are recorded separately from steady-frame statistics.
    for(int mode=0;mode<2;++mode){auto sample=Measure(*rigs[mode],config,0,query);Validate(config,sample,mode==1,true);WriteSample(raw,config,mode,"cold",-1,0,sample);}
    {
        Rig reference(config,true,false);
        for(int frame:{1,2,7,19,0,23}){
            const auto expected=Capture(reference,config,frame);
            Check(reference.renderer.GetShadowStats().faces==size_t(config.Faces()),"Reference must force every depth view");
            for(int mode=0;mode<2;++mode){
                const auto actual=Capture(*rigs[mode],config,frame);Validate(config,Stats(rigs[mode]->renderer),mode==1);
                int maxError=0;size_t overOne=0;for(size_t i=0;i<expected.size();++i){const int error=std::abs(int(expected[i])-int(actual[i]));maxError=std::max(maxError,error);overOne+=error>1;}
                parity<<config.Name()<<','<<frame<<','<<Modes[mode]<<','<<maxError<<','<<overOne<<'\n';
                Check(overOne==0,"Sequential cached shadow image differs from full redraw in "+config.Name()+"/"+Modes[mode]);
            }
        }
    }
    std::vector<Sample> samples[2];
    for(int repeat=0;repeat<repeats;++repeat)for(int slot=0;slot<2;++slot){
        const int mode=(repeat+slot)%2;
        for(int frame=-Warmup;frame<frames;++frame){
            auto sample=Measure(*rigs[mode],config,frame+Warmup,query);
            if(frame>=0){Validate(config,sample,mode==1);samples[mode].push_back(sample);WriteSample(raw,config,mode,"warm",repeat,frame,sample);}
        }
    }
    glDeleteQueries(1,&query);
    for(int mode=0;mode<2;++mode){
        std::vector<double> cpu,complete,gpu;double updates=0,faces=0,candidates=0,submitted=0,draws=0,instances=0,reflection=0,bounds=0,changedCasters=0,changedBounds=0,affectedLights=0,dirtyBuilds=0;
        for(const auto& sample:samples[mode]){cpu.push_back(sample.cpu);complete.push_back(sample.complete);gpu.push_back(sample.gpu);updates+=sample.updates;faces+=sample.faces;candidates+=sample.candidates;submitted+=sample.submitted;draws+=sample.draws;instances+=sample.instances;reflection+=sample.reflectionFaces;bounds+=sample.boundsUpdates;changedCasters+=sample.changedCasters;changedBounds+=sample.changedBounds;affectedLights+=sample.affectedLights;dirtyBuilds+=sample.dirtyBVHBuilds;}
        const double count=double(samples[mode].size());
        summary<<config.Name()<<','<<config.objects<<','<<config.lighting<<','<<config.Lights()<<','<<config.motion<<','<<config.reflection<<','<<Modes[mode]<<','<<samples[mode].size()
               <<','<<Quantile(cpu,.5)<<','<<Quantile(cpu,.95)<<','<<Quantile(complete,.5)<<','<<Quantile(complete,.95)<<','<<Quantile(gpu,.5)<<','<<Quantile(gpu,.95)
               <<','<<updates/count<<','<<faces/count<<','<<candidates/count<<','<<submitted/count<<','<<draws/count<<','<<instances/count<<','<<reflection/count<<','<<bounds/count
               <<','<<changedCasters/count<<','<<changedBounds/count<<','<<affectedLights/count<<','<<dirtyBuilds/count<<'\n';
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
    Check(argc>=2,"Usage: RiceShadowInvalidationBenchmark <output-directory> [frames=25] [repeats=4] [case-substring]");
    const int frames=argc>2?std::stoi(argv[2]):25,repeats=argc>3?std::stoi(argv[3]):4;Check(frames>=5&&repeats>=1,"Need at least five frames and one repeat");
    const std::string filter=argc>4?argv[4]:"";auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    Debug::Init();Check(glfwInit()!=0,"GLFW init");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(Width,Height,"Selective shadow invalidation benchmark",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);glfwSwapInterval(0);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
    RenderCommand::SetDepthTestEnabled(true);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);GameObject::SetWorldCacheEnabled(true);
    std::ofstream metadata(output/"metadata.txt"),raw(output/"frames.csv"),summary(output/"summary.csv"),parity(output/"parity.csv");Check(bool(metadata)&&bool(raw)&&bool(summary)&&bool(parity),"Cannot open output files");
    metadata<<"GPU: "<<glGetString(GL_RENDERER)<<"\nOpenGL: "<<glGetString(GL_VERSION)<<"\nCPU: "<<CpuName()<<"\nBuild: "<<__DATE__<<' '<<__TIME__
            <<"\nMain view: "<<Width<<'x'<<Height<<"\nShadow resolution: "<<ShadowSize<<"\nWarmup frames: "<<Warmup<<"\nMeasured frames per mode/run: "<<frames<<"\nRepeats: "<<repeats<<"\nFilter: "<<filter<<'\n'
            <<"Modes: global(previous all-caster revision invalidates every light), selective(previous/current changed bounds invalidate affected lights). Same executable. Both retain shadow culling, depth caching, world/bounds caches, cube instancing and reflection culling.\n"
            <<"Both modes gather the new common flat caster snapshot with bounds/record metadata. Global mode skips per-caster differencing and changed-bounds BVH construction; its changed_casters/changed_bounds/dirty_bvh_builds counters are zero(not a claim that objects are static). Selective tracking/BVH costs are included in the comparison.\n"
            <<"Independent Scene+SceneRenderer+ViewportTarget per mode; no shared shadow or spatial history. Alternating mode order each repeat balances order positions with four repeats. Same deterministic frames replayed per mode. No profiler session.\n"
            <<"Separated point lights: 52-unit cluster spacing, point range20, height12; each caster cluster occupies16x16. 80% cubes/20% rectangles with varied scales/heights/yaw. First10% of objects move within one cluster; distributed mode moves every tenth object across all clusters.\n"
            <<"Point4 has4 separate clusters, point8 has8, mixed8 has1directional+7separate points. Overlap8 places8points over one cluster and forces allaffected; directional1 also forces allaffected. Outside mode moves10% relocated250units beyond all point volumes. Static is a cache-hit control.\n"
            <<"Camera position depends on fixed cluster extent(22/65/115):(.35extent,.9extent,1.3extent), looking at(0,1,0),55degree FOV. Main size remains fixed. Reflection control adds one mirror caster/receiver in cluster0;64x64 probe is forced to capture6faces each frame in both modes.\n"
            <<"First Render per independent mode is recorded as cold; includes lazy GL resources but excludes scene/renderer construction. Warm p50/p95 use nearest-rank quantiles pooled across repeats.\n"
            <<"CPU timing includes deterministic motion, viewport clear/setup, cache validation/BVH maintenance and render submission. Completion waits glFinish. GL_TIME_ELAPSED brackets renderer.Render only(excludes motion/viewport setup), and can include command starvation. Drained before/after each sample; serialized costs, not production FPS.\n"
            <<"Every measured frame validates expected depth maps/faces. Candidates are eligible caster components times actually rendered depth faces; submitted is post-frustum-cull components, not BVH node tests.\n"
            <<"Invalidation counters: changed caster records, previous/current bounds retained for invalidation, caster-affected lights, and changed-bounds BVH builds. Light movement is not part of these workloads.\n"
            <<"Sequential parity frames1,2,7,19,0,23 compare each cached mode against a third independent cache-disabled renderer at identical time. Readback is outside timed workload; tolerance1 channel value. Histories advance independently through all frames, including rewind.\n";
    raw<<"case,objects,lighting,lights,motion,reflection,mode,phase,repeat,frame,cpu_ms,completion_ms,gpu_ms,shadow_updates,shadow_faces,shadow_candidates,shadow_submitted,shadow_draws,shadow_instances,reflection_faces,bounds_updates,changed_casters,changed_bounds,affected_lights,dirty_bvh_builds\n";
    summary<<"case,objects,lighting,lights,motion,reflection,mode,samples,cpu_p50_ms,cpu_p95_ms,completion_p50_ms,completion_p95_ms,gpu_p50_ms,gpu_p95_ms,shadow_updates_mean,shadow_faces_mean,shadow_candidates_mean,shadow_submitted_mean,shadow_draws_mean,shadow_instances_mean,reflection_faces_mean,bounds_updates_mean,changed_casters_mean,changed_bounds_mean,affected_lights_mean,dirty_bvh_builds_mean\n";
    parity<<"case,frame,mode,max_channel_error,channels_over_one\n";raw<<std::fixed<<std::setprecision(6);summary<<std::fixed<<std::setprecision(6);
    std::vector<Case> cases;for(int count:{100,1000,3000})for(const char* lights:{"point4","point8","mixed8"})cases.push_back({count,lights,"local"});
    for(int count:{1000,3000})cases.push_back({count,"point8","distributed"});
    cases.push_back({3000,"overlap8","local"});cases.push_back({3000,"directional1","local"});cases.push_back({1000,"point8","static"});cases.push_back({1000,"point8","outside"});cases.push_back({1000,"mixed8","local",true});
    size_t executed=0;for(const auto& config:cases)if(filter.empty()||config.Name().find(filter)!=std::string::npos){Run(config,frames,repeats,raw,summary,parity);++executed;}
    Check(executed>0,"Filter matched no cases");metadata<<"Completed scenarios: "<<executed<<'\n';Check(bool(raw)&&bool(summary)&&bool(parity),"Failed writing benchmark results");
    glfwDestroyWindow(window);glfwTerminate();std::cout<<"Shadow invalidation benchmark complete: "<<output.string()<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
