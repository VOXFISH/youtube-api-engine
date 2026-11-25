#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include "Rice/Debug.h"
#include "Project/Project.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "../Scripts/PresentationMotion.h"
#include "PresentationVideo.h"
#include <cstring>
#include <iostream>

using namespace Rice;
namespace {
constexpr int Width=1920,Height=1080,Fps=60,Frames=13*Fps;
constexpr const char* Phases[]{
    "공 방출 · 큐브 정지", "큐브 이동 · 섀도맵 갱신", "큐브 정지 · 캐시 재사용",
    "큐브 복귀 · 섀도맵 갱신", "복귀 완료 · 캐시 재사용"
};

struct CaptureContext {
    GLFWwindow* window=nullptr;
    bool gui=false;
    ImVector<ImWchar> glyphs;
    CaptureContext(){
        if(!glfwInit())throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        window=glfwCreateWindow(Width,Height,"Shadow presentation recording",nullptr,nullptr);
        if(!window){glfwTerminate();throw std::runtime_error("Recording context failed");}
        glfwMakeContextCurrent(window);
        if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
            glfwDestroyWindow(window);glfwTerminate();throw std::runtime_error("OpenGL loader failed");
        }
        IMGUI_CHECKVERSION();ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.DisplaySize={float(Width),float(Height)};io.DisplayFramebufferScale={1,1};io.DeltaTime=1.f/Fps;
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf",27.f);
        ImFontGlyphRangesBuilder ranges;for(auto* phase:Phases)ranges.AddText(phase);
        ranges.BuildRanges(&glyphs);
        if(!io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf",28.f,nullptr,glyphs.Data))
            throw std::runtime_error("Presentation caption font unavailable");
        if(!ImGui_ImplOpenGL3_Init("#version 330"))throw std::runtime_error("Recording UI initialization failed");
        gui=true;
    }
    ~CaptureContext(){
        if(gui){ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();}
        if(window)glfwDestroyWindow(window);glfwTerminate();
    }
};

void DrawPresentationUI(Scene& scene,double elapsed){
    ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
    auto* draw=ImGui::GetForegroundDrawList();
    // Preserve the saved Game-view UI at its 1280x720 design scale.
    for(auto& object:scene.GetObjects())if(object->IsActive())
        if(auto* text=object->GetComponent<UIText>();text&&text->enabled){
            auto c=text->color;
            draw->AddText(ImGui::GetIO().Fonts->Fonts[0],text->size*1.5f,
                {text->position.x*1.5f,text->position.y*1.5f},
                ImGui::ColorConvertFloat4ToU32({c.x,c.y,c.z,c.w}),text->text.c_str());
        }
    const int phase=elapsed<3?0:elapsed<5?1:elapsed<8?2:elapsed<10?3:4;
    draw->AddText(ImGui::GetIO().Fonts->Fonts[1],28.f,{660,213},IM_COL32(236,242,248,255),Phases[phase]);
    draw->AddRectFilled({660,254},{1740,258},IM_COL32(50,60,70,255));
    draw->AddRectFilled({660,254},{660+1080.f*float(elapsed/13.),258},IM_COL32(95,235,210,255));
    ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
}

int main(int argc,char** argv){try{
    if(argc!=3&&argc!=4)throw std::runtime_error("Usage: RiceShadowRecording <project-directory> <output.mp4> [verification-directory]");
    const auto root=fs::absolute(argv[1]),output=fs::absolute(argv[2]);
    if(fs::exists(output))throw std::runtime_error("Choose a new output filename");
    fs::create_directories(output.parent_path());
    const auto verification=fs::absolute(argc==4?fs::path(argv[3]):fs::path("Engine/Cache/recording-verification"));
    fs::create_directories(verification);
    Debug::Init();CaptureContext context;
    ComponentRegistry registry;RegisterPresentationComponents(registry);
    Project project(root,true);
    auto scene=project.LoadScene(registry);
    auto& assets=project.assets;
    ShadowPresentation* demo=nullptr;
    for(auto& object:scene->GetObjects()){
        if(auto* material=object->GetComponent<Material>();material&&!material->albedoAsset.empty())
            material->albedoTexture=assets.Texture(material->albedoAsset);
        if(auto* controller=object->GetComponent<ShadowPresentation>())demo=controller;
    }
    if(!demo||!demo->autoMotion)throw std::runtime_error("An automatic Shadow Presentation controller is required");
    if(demo->warmupSeconds!=3||demo->moveSeconds!=2||demo->holdSeconds!=3)
        throw std::runtime_error("This 13-second recording expects the saved 3/2/3-second presentation sequence");
    RiceController::Streaming::OutputProfile profile;
    auto encoder=RiceController::Streaming::OpenHardwareVideoEncoder(profile);
    PresentationVideo::Mp4Writer movie(output,encoder->Format());
    auto csvPath=verification/output.filename();csvPath.replace_extension(".csv");
    std::ofstream csv(csvPath);csv<<"frame,time_seconds,visible_balls,shadow_faces,shadow_updates,bvh_ray_queries,path_rebuilds,cube_x,cube_y,cube_z\n";
    SceneRenderer renderer;ViewportTarget target;
    PerspectiveCamera fallback(44,float(Width)/Height,.1f,100);
    std::vector<std::uint8_t> bottomUp(Width*Height*4),topDown(bottomUp.size());
    size_t rebuiltFrames=0,cachedFrames=0,movingRebuilds=0,heldCacheFrames=0;
    for(int frame=0;frame<Frames;++frame){
        glfwPollEvents();const float dt=frame?1.f/Fps:0.f;
        scene->Update(dt);scene->LateUpdate(dt);
        target.Render(Width,Height,[&]{
            RenderCommand::SetDepthTestEnabled(true);
            auto& camera=CameraComponent::GameView(*scene,float(Width)/Height,fallback);
            renderer.Render(*scene,camera);ObservePresentation(*scene,renderer);scene->Render();
            DrawPresentationUI(*scene,demo->Elapsed());
            glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,bottomUp.data());
        });
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL error while recording");
        for(int y=0;y<Height;++y)std::memcpy(topDown.data()+size_t(y)*Width*4,bottomUp.data()+size_t(Height-1-y)*Width*4,Width*4);
        for(const auto& packet:encoder->EncodeRgba(topDown,Width*4,frame))movie.Write(packet);
        const auto& stats=demo->GetStats();auto* cube=scene->Find(demo->cube);
        auto position=cube->GetWorldPivot();const double t=double(frame)/Fps;
        csv<<frame<<','<<std::fixed<<std::setprecision(6)<<t<<','<<stats.visibleBalls<<','<<stats.shadowFaces<<','<<stats.shadowUpdates<<','<<stats.rayQueries<<','<<stats.pathRebuilds<<','<<position.x<<','<<position.y<<','<<position.z<<'\n';
        if(stats.shadowFaces)++rebuiltFrames;else ++cachedFrames;
        if(t>3.1&&t<4.9){if(stats.shadowFaces!=6||stats.rayQueries==0)throw std::runtime_error("Moving interval did not rebuild actual shadow maps and ray paths");++movingRebuilds;}
        if(t>5.2&&t<7.8){if(stats.shadowFaces!=0||stats.rayQueries!=0||stats.visibleBalls==0)throw std::runtime_error("Held interval did not reuse cache while balls kept moving");++heldCacheFrames;}
        if(frame==0&&stats.visibleBalls!=0)throw std::runtime_error("Opening frame must precede emission");
        if(frame%Fps==0)std::cout<<"Captured "<<frame/Fps<<" / 13 seconds: "<<stats.shadowFaces<<" shadow faces, "<<stats.visibleBalls<<" balls\n"<<std::flush;
    }
    for(const auto& packet:encoder->Drain())movie.Write(packet);
    movie.Finish();csv.close();if(!csv)throw std::runtime_error("Recording telemetry write failed");
    if(!movingRebuilds||!heldCacheFrames)throw std::runtime_error("Recording did not cover both movement and cache reuse");
    const auto verified=PresentationVideo::Verify(output,Width,Height,Fps,Frames,true,verification);
    std::cout<<"Verified "<<verified.frames<<" frames, "<<verified.durationSeconds<<" seconds. Rebuild frames: "<<rebuiltFrames<<"; cached frames: "<<cachedFrames<<"\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
