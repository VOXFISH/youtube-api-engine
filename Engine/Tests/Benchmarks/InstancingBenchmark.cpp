// Reproducible renderer A/B benchmark. This is a manual benchmark, not a CTest.
#include "Project/Workspace.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/ViewportTarget.h"
#include "../../Sandbox/Scripts/GameComponents.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

using namespace Rice;
using Clock = std::chrono::steady_clock;
namespace {
constexpr int Width=1920,Height=1080,Warmup=60;
double Ms(Clock::time_point start,Clock::time_point end){return std::chrono::duration<double,std::milli>(end-start).count();}
struct Sample { double renderMs=0,frameMs=0,gpuMs=0;uint32_t draws=0,batches=0,instances=0; };
struct Summary { double mean=0,p95=0,maximum=0; };
Summary Summarize(std::vector<double> values){
    Summary result;result.mean=std::accumulate(values.begin(),values.end(),0.0)/double(values.size());
    std::sort(values.begin(),values.end());result.p95=values[size_t(std::ceil(double(values.size())*.95))-1];result.maximum=values.back();return result;
}
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Position(PerspectiveCamera& camera,const Vector3& center,int frame,int count,bool moving){
    const float phase=float(frame)/float(count)*glm::two_pi<float>();
    const float angle=moving?.35f*std::sin(phase):0.f;
    camera.SetPosition(center+Vector3(40*std::sin(angle),15+(moving?3*std::sin(phase*2):0),40*std::cos(angle)));
    camera.LookAtObj(center);
}
std::vector<Sample> Run(Workspace& workspace,SceneRenderer& renderer,ViewportTarget& target,
                       PerspectiveCamera& camera,Vector3 center,bool instancing,bool moving,int count,int passes){
    renderer.SetCubeInstancingEnabled(instancing);
    std::vector<Sample> samples;samples.reserve(size_t(count));
    GLuint query=0;glGenQueries(1,&query);
    for(int index=-Warmup;index<count;++index){
        Sample sample;
        const auto begin=Clock::now();
        {
            BenchmarkFrame frame;
            workspace.Update(1.f/60,true);
            Position(camera,center,std::max(index,0),count,moving);
            target.Render(Width,Height,[&]{
                glBeginQuery(GL_TIME_ELAPSED,query);
                const auto renderBegin=Clock::now();
                for(int pass=0;pass<passes;++pass){
                    if(pass)glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                    renderer.Render(*workspace.editor.scene,camera);
                    auto stats=renderer.GetRenderStats();
                    sample.draws+=stats.drawCalls;sample.batches+=stats.instancedDrawCalls;sample.instances+=stats.instancedInstances;
                }
                sample.renderMs=Ms(renderBegin,Clock::now());
                glEndQuery(GL_TIME_ELAPSED);
            });
            // Deliberately serialize GPU completion: frameMs is workload completion
            // cost, not editor FPS. CPU submission is measured before this wait.
            glFinish();
            GLuint64 gpu=0;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&gpu);sample.gpuMs=double(gpu)/1e6;
        }
        // Includes profiler queue backpressure in BeginFrame/EndFrame.
        sample.frameMs=Ms(begin,Clock::now());
        if(index>=0)samples.push_back(sample);
    }
    glDeleteQueries(1,&query);
    Check(glGetError()==GL_NO_ERROR,"OpenGL error during benchmark");
    const uint32_t cubes=1000u*uint32_t(passes);
    for(const auto& sample:samples){
        Check(sample.draws==(instancing?uint32_t(passes):cubes),"Unexpected Grass1000 draw count");
        Check(sample.instances==(instancing?cubes:0u),"Unexpected instance count");
    }
    return samples;
}
std::vector<unsigned char> Capture(Workspace& workspace,SceneRenderer& renderer,ViewportTarget& target,
                                 PerspectiveCamera& camera,Vector3 center,bool instanced,int frame,int count){
    renderer.SetCubeInstancingEnabled(instanced);Position(camera,center,frame,count,true);
    std::vector<unsigned char> pixels(size_t(Width)*Height*4);
    target.Render(Width,Height,[&]{renderer.Render(*workspace.editor.scene,camera);glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    return pixels;
}
void WritePpm(const fs::path& path,const std::vector<unsigned char>& pixels){
    std::ofstream file(path,std::ios::binary);file<<"P6\n"<<Width<<' '<<Height<<"\n255\n";
    for(int y=Height-1;y>=0;--y)for(int x=0;x<Width;++x)file.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*Width+x)*4),3);
    Check(bool(file),"Cannot save benchmark image");
}
}

int main(int argc,char** argv){
    GLFWwindow* window=nullptr;
    try{
        Check(argc>=3,"Usage: RiceInstancingBenchmark <project> <output> [frames=240] [pairs=3] [passes=1]");
        const fs::path project=fs::absolute(argv[1]),output=fs::absolute(argv[2]);
        const int count=argc>3?std::stoi(argv[3]):240,pairs=argc>4?std::stoi(argv[4]):3,passes=argc>5?std::stoi(argv[5]):1;
        Check(count>=30&&pairs>=1&&passes>=1&&passes<=3,"Invalid benchmark settings");
        fs::create_directories(output);Debug::Init();Check(glfwInit()!=0,"GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window=glfwCreateWindow(Width,Height,"Rice instancing benchmark",nullptr,nullptr);Check(window!=nullptr,"OpenGL context failed");
        glfwMakeContextCurrent(window);glfwSwapInterval(0);Check(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))!=0,"GLAD initialization failed");
        std::ofstream metadata(output/"metadata.txt");
        metadata<<"GPU: "<<glGetString(GL_RENDERER)<<"\nOpenGL: "<<glGetString(GL_VERSION)<<"\nProject: "<<project.generic_string()
                <<"\nViewport: "<<Width<<'x'<<Height<<"\nFrames per run: "<<count<<"\nWarmup per run: "<<Warmup
                <<"\nPairs per condition: "<<pairs<<"\nScene renders per frame: "<<passes
                <<"\nOrder: alternating AB/BA pairs, A=individual, B=instanced\n"
                <<"Frame_ms includes update, camera changes, GPU completion, profiler queue backpressure; excludes editor UI and swap.\n"
                <<"Render_ms is CPU submission, before glFinish. GPU elapsed includes command stream gaps.\n";
        std::ofstream raw(output/"frames.csv"),summary(output/"summary.csv");
        raw<<"logging,moving,pair,instancing,frame,render_ms,completion_ms,gpu_elapsed_ms,draw_calls,instanced_draws,instances\n";
        summary<<"logging,moving,pair,instancing,render_mean_ms,render_p95_ms,render_max_ms,completion_mean_ms,completion_p95_ms,completion_max_ms,gpu_mean_ms,gpu_p95_ms,draw_calls,instanced_draws,instances\n";
        raw<<std::setprecision(9);summary<<std::setprecision(9);
        {
            Workspace workspace(project,true,RegisterGameComponents);SceneRenderer renderer;ViewportTarget target;
            PerspectiveCamera camera(45,float(Width)/Height,.1f,150);
            workspace.Play();workspace.Update(1.f/60,true);
            size_t cubes=0,otherMeshes=0,probes=0;Vector3 center(0);
            for(auto& object:workspace.editor.scene->GetObjects())if(object->IsActive()){
                if(auto* mesh=object->GetComponent<MeshRenderer>();mesh&&mesh->enabled){
                    if(object->name.starts_with("Grass Cube")&&mesh->shape==int(BasicShape::Cube)){
                        ++cubes;center+=Vector3(object->GetWorldMatrix()[3]);
                        auto* material=object->GetComponent<Material>();Check(material&&material->albedoTexture,"Grass texture missing");
                    }else ++otherMeshes;
                }
                if(auto* probe=object->GetComponent<ReflectionProbe>();probe&&probe->enabled)++probes;
            }
            Check(cubes==1000&&otherMeshes==0&&probes==0,"Expected Grass1000 scene without reflection probes");center/=float(cubes);
            metadata<<"Grass cubes: "<<cubes<<"\nCenter: "<<center.x<<','<<center.y<<','<<center.z<<'\n';
            glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
            // Validate real scene output at three deterministic points, outside samples.
            size_t changed=0;int maxDifference=0;
            for(int frame:{0,count/4,count/2}){
                auto before=Capture(workspace,renderer,target,camera,center,false,frame,count);
                auto after=Capture(workspace,renderer,target,camera,center,true,frame,count);
                for(size_t i=0;i<before.size();++i){int difference=std::abs(int(before[i])-int(after[i]));maxDifference=std::max(maxDifference,difference);if(difference>1)++changed;}
                if(!frame){WritePpm(output/"individual.ppm",before);WritePpm(output/"instanced.ppm",after);}
            }
            metadata<<"Image max channel difference: "<<maxDifference<<"\nImage channels differing by >1: "<<changed<<'\n';metadata.flush();
            Check(changed==0,"Instanced Grass1000 image does not match individual draws");
            for(bool logging:{false,true})for(bool moving:{false,true})for(int pair=0;pair<pairs;++pair){
                std::array<bool,2> order=pair%2?std::array{true,false}:std::array{false,true};
                for(bool instanced:order){
                    std::unique_ptr<BenchmarkSession> session;
                    if(logging){
                        session=std::make_unique<BenchmarkSession>(output/"Logs"/(std::string(moving?"moving-":"still-")+(instanced?"instanced-":"individual-")+std::to_string(pair)+".tsv"));
                        Benchmark::BeginPlay();
                        Check(Benchmark::Schedule(Warmup+1,static_cast<uint32_t>(count),1),"Cannot schedule measured renderer frames");
                    }
                    auto samples=Run(workspace,renderer,target,camera,center,instanced,moving,count,passes);
                    session.reset(); // Drain all logging before the next run, outside timing.
                    std::vector<double> renders,completions,gpus;
                    for(size_t index=0;index<samples.size();++index){const auto& s=samples[index];
                        raw<<logging<<','<<moving<<','<<pair<<','<<instanced<<','<<index<<','<<s.renderMs<<','<<s.frameMs<<','<<s.gpuMs<<','<<s.draws<<','<<s.batches<<','<<s.instances<<'\n';
                        renders.push_back(s.renderMs);completions.push_back(s.frameMs);gpus.push_back(s.gpuMs);
                    }
                    auto cpu=Summarize(renders),whole=Summarize(completions),gpu=Summarize(gpus);const auto& s=samples.front();
                    summary<<logging<<','<<moving<<','<<pair<<','<<instanced<<','<<cpu.mean<<','<<cpu.p95<<','<<cpu.maximum<<','<<whole.mean<<','<<whole.p95<<','<<whole.maximum<<','<<gpu.mean<<','<<gpu.p95<<','<<s.draws<<','<<s.batches<<','<<s.instances<<'\n';
                    raw.flush();summary.flush();
                    std::cout<<"log="<<logging<<" moving="<<moving<<" pair="<<pair<<" instanced="<<instanced<<" CPU="<<cpu.mean<<"ms completed="<<whole.mean<<"ms GPU="<<gpu.mean<<"ms draws="<<s.draws<<std::endl;
                }
            }
            workspace.Stop();
        }
        glfwDestroyWindow(window);glfwTerminate();return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}
}
