#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "Project/Project.h"
#include "Project/ShapeFile.h"
#include "Renderer/ViewportTarget.h"
#include "../Scripts/PresentationMotion.h"
#include <iostream>

using namespace Rice;
namespace {
GameObject& Box(Scene& scene,const std::string& name,Vector3 position,Vector3 scale,Color color,bool casts=true,int shape=0){
    auto& object=scene.CreateGameObject(name);object.transform.position=position;object.transform.localScale=scale;
    auto& mesh=object.AddComponent<MeshRenderer>();mesh.color=color;mesh.castsShadow=casts;mesh.shape=shape;return object;
}
GameObject& Outline(Scene& scene,SpatialBounds bounds,const std::string& name="02 | Steve AABB - cyan hollow box",Color color={.15f,3.f,4.f,1}){
    auto& group=scene.CreateGameObject(name);
    int index=0;const float width=.025f;
    for(int axis=0;axis<3;++axis)for(int a=0;a<2;++a)for(int b=0;b<2;++b){
        auto center=(bounds.low+bounds.high)*.5f;auto scale=Vector3(width);
        scale[axis]=bounds.high[axis]-bounds.low[axis]+width;
        int u=(axis+1)%3,v=(axis+2)%3;center[u]=a?bounds.high[u]:bounds.low[u];center[v]=b?bounds.high[v]:bounds.low[v];
        auto& edge=Box(scene,"AABB edge "+std::to_string(++index),center,scale,color,false);edge.SetParent(&group);
    }
    return group;
}
void BMP(const fs::path& path,const std::vector<unsigned char>& pixels,int width,int height){
    std::ofstream out(path,std::ios::binary);auto byte=[&](unsigned n){out.put(char(n));};auto word=[&](unsigned n){byte(n);byte(n>>8);};auto dword=[&](unsigned n){word(n);word(n>>16);};
    byte('B');byte('M');dword(54+width*height*3);dword(0);dword(54);dword(40);dword(width);dword(height);word(1);word(24);dword(0);dword(width*height*3);dword(0);dword(0);dword(0);dword(0);
    for(int i=0;i<width*height;++i){byte(pixels[i*4+2]);byte(pixels[i*4+1]);byte(pixels[i*4]);}
    if(!out)throw std::runtime_error("Preview write failed");
}
}
int main(int argc,char** argv){GLFWwindow* window=nullptr;try{
    if(argc!=2&&argc!=3)throw std::runtime_error("Usage: RiceShadowPresentation <project-directory> [preview-directory]");
    auto root=fs::absolute(argv[1]);fs::create_directories(root/"Assets");
    auto previews=fs::absolute(argc==3?fs::path(argv[2]):fs::path("Engine/Cache/presentation-preview"));fs::create_directories(previews);
    Debug::Init();ComponentRegistry registry;RegisterPresentationComponents(registry);Scene scene;
    scene.settings.ground=true;scene.settings.groundCollision=false;scene.settings.directionalLight=false;scene.settings.axes=Vector3(0);scene.settings.grids=Vector3(0);
    auto& sequence=scene.CreateGameObject("00 | DEMO CONTROLLER - automatic move / hold / return");
    auto& demo=sequence.AddComponent<ShadowPresentation>();
    auto& steve=scene.CreateGameObject("01 | Steve - original skin, posed mesh");steve.transform.position={.6f,0,0};
    auto& skin=steve.AddComponent<MeshRenderer>();skin.shape=int(BasicShape::Custom);skin.custom=ShapeFile::Load(root/"Assets"/"SteveHolding.riceshape");skin.color=Color(1);skin.shapeAsset="Assets/SteveHolding.riceshape";
    auto& cloth=steve.AddComponent<Material>();cloth.albedoAsset="36fa91f9aff206d09d4fb534ec11e645";cloth.roughness=.9f;
    auto [low,high]=skin.custom.Bounds();auto bounds=SpatialBounds::Transform(steve.GetWorldMatrix(),{low,high});Outline(scene,bounds);
    auto& metal=Box(scene,"03 | Held tilted metal cube",{1.95f,2.12f,1.12f},Vector3(.72f),{.88f,.94f,1.f,1});metal.transform.rotation={18,-24,24};
    auto& silver=metal.AddComponent<Material>();silver.metallicity=.94f;silver.roughness=.13f;
    auto& probe=metal.AddComponent<ReflectionProbe>();probe.resolution=256;probe.maxObjectSize=0;
    auto cubeBounds=SpatialBounds::Transform(metal.GetWorldMatrix(),{Vector3(-.5f),Vector3(.5f)});
    auto& oldBox=Outline(scene,cubeBounds,"07 | PREVIOUS cube bounds - amber",{4.f,1.5f,.1f,1});oldBox.SetActive(false);
    auto& currentBox=Outline(scene,cubeBounds,"08 | CURRENT cube bounds - green",{.3f,3.f,.7f,1});
    Vector3 source{-2.8f,5.6f,3.5f};
    auto& lamp=Box(scene,"04 | POINT LIGHT - one actual light",source,Vector3(.28f),{7,4,1,1},false,int(BasicShape::Sphere));
    auto& light=lamp.AddComponent<Light>();light.type=0;light.intensity=11;light.range=16;light.color={1.f,.94f,.84f,1};
    // A small geometric halo makes the emitter readable without adding lights.
    for(int i=0;i<24;++i){float angle=float(i)*6.2831853f/24;auto p=source+Vector3(.34f*std::cos(angle),.34f*std::sin(angle),0);
        Box(scene,"Emitter halo "+std::to_string(i),p,Vector3(.035f),{5,2.5f,.4f,1},false,int(BasicShape::Sphere));}
    auto& rays=scene.CreateGameObject("05 | Light ray markers - Play to animate");
    const std::vector<Vector3> targets{{.6f,2.8f,.25f},{.6f,1.8f,.25f},{1.75f,1.95f,1.12f},{1.95f,2.12f,1.12f},{2.2f,2.3f,1.12f},{2.6f,2.15f,1.1f},{2.95f,2.35f,1.05f},{3.2f,2.3f,.95f},{-.8f,0,2.f},{2.5f,0,2.8f},{-3.3f,0,-.8f},{.8f,0,-2.4f}};
    int ray=0;
    for(auto target:targets){auto direction=glm::normalize(target-source);
        for(int bead=0;bead<7;++bead){
            auto& object=Box(scene,"Ray "+std::to_string(ray+1)+" / ball "+std::to_string(bead+1),source,Vector3(.08f),{5,3,.45f,1},false,int(BasicShape::Sphere));
            object.SetParent(&rays);auto& motion=object.AddComponent<PresentationBall>();motion.direction=direction;motion.maxDistance=10.f;
            motion.speed=3.2f;motion.delaySeconds=float(bead)*.48f+float(ray)*.025f+.12f;motion.repeatSeconds=3.36f;motion.diameter=.08f;
            object.GetComponent<MeshRenderer>()->enabled=false;
        }++ray;
    }
    auto& cameraObject=scene.CreateGameObject("06 | RECORDING CAMERA - use Game view");cameraObject.transform.position={4.4f,4.8f,9.f};
    auto& camera=cameraObject.AddComponent<CameraComponent>();camera.fieldOfView=44;camera.LookAt({-.5f,2.7f,.3f});
    auto& status=scene.CreateGameObject("09 | LIVE recomputation status - Game view overlay");
    auto& text=status.AddComponent<UIText>();text.position={440,22};text.size=18;text.color={.12f,.95f,.9f,1};text.text="PRESS PLAY | light emission + moving cube";
    demo.source=lamp.GetID();demo.steve=steve.GetID();demo.cube=metal.GetID();demo.status=status.GetID();demo.oldBoundsGroup=oldBox.GetID();demo.currentBoundsGroup=currentBox.GetID();
    demo.autoMotion=true;demo.warmupSeconds=3;demo.moveSeconds=2;demo.holdSeconds=3;demo.offset={1.05f,.25f,-.15f};
    Project project(root);
    project.SaveScene(scene,registry,project.ActiveScene());
    auto decoded=Project(project.FilePath(),true).LoadScene(registry);
    if(decoded->GetObjectCount()!=scene.GetObjectCount())throw std::runtime_error("Scene round-trip count mismatch");
    if(!glfwInit())throw std::runtime_error("GLFW init failed");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(1280,720,"Presentation preview",nullptr,nullptr);if(!window)throw std::runtime_error("Preview context failed");glfwMakeContextCurrent(window);if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))throw std::runtime_error("GL load failed");
    {
        cloth.albedoTexture=Texture2D::Create(ProjectFile::Utf8(root/"Assets"/"Steve.png"));
        RenderCommand::SetDepthTestEnabled(true);RenderCommand::SetClearColor({.045f,.055f,.075f,1});
        SceneRenderer renderer;ViewportTarget target;renderer.SetShadowResolution(512);
        std::vector<unsigned char> pixels(1280*720*4);
        auto capture=[&](const char* name){target.Render(1280,720,[&]{renderer.Render(scene,camera.View(1280.f/720));ObservePresentation(scene,renderer);glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});BMP(previews/name,pixels,1280,720);};
        scene.Update(0);scene.LateUpdate(0);
        capture("preview.bmp");
        for(int frame=0;frame<25;++frame){scene.Update(.1f);scene.LateUpdate(.1f);}
        capture("preview-emission.bmp");
        for(int frame=0;frame<15;++frame){scene.Update(.1f);scene.LateUpdate(.1f);}
        capture("preview-motion.bmp");
        if(renderer.GetShadowStats().faces!=6)throw std::runtime_error("Moving cube must refresh six point-shadow faces");
        for(int frame=0;frame<25;++frame){scene.Update(.1f);scene.LateUpdate(.1f);}
        capture("preview-hold.bmp");
        for(int frame=0;frame<5;++frame){scene.Update(.1f);scene.LateUpdate(.1f);}
        capture("preview-cached.bmp");
        if(renderer.GetShadowStats().faces!=0)throw std::runtime_error("Holding cube must reuse point-shadow cache");
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL error in preview");
        cloth.albedoTexture.reset();
    }
    std::cout<<"Created "<<scene.GetObjectCount()<<" objects, 84 delayed ray balls, live cube AABBs and shadow telemetry, 1 point light.\nAABB "<<bounds.low.x<<','<<bounds.low.y<<','<<bounds.low.z<<" -> "<<bounds.high.x<<','<<bounds.high.y<<','<<bounds.high.z<<'\n';
    glfwDestroyWindow(window);glfwTerminate();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}

