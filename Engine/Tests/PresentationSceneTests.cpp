#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "../Sandbox/Scripts/PresentationMotion.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <iostream>
#include <string>

using namespace Rice;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    Scene scene;
    GameObject *control=nullptr,*cube=nullptr,*lamp=nullptr,*label=nullptr;
    ShadowPresentation* demo=nullptr;
    std::vector<GameObject*> balls;
    Fixture(bool automatic=false){
        scene.settings.ground=true;scene.settings.directionalLight=false;scene.settings.groundCollision=false;
        control=&scene.CreateGameObject("Presentation controller");demo=&control->AddComponent<ShadowPresentation>();demo->autoMotion=automatic;
        demo->warmupSeconds=1;demo->moveSeconds=1;demo->holdSeconds=1;demo->offset={3,0,0};
        lamp=&scene.CreateGameObject("Light");lamp->transform.position={0,4,0};auto& light=lamp->AddComponent<Light>();light.range=12;light.intensity=2;
        cube=&scene.CreateGameObject("Moving cube");cube->transform.position={0,1,0};cube->AddComponent<MeshRenderer>();
        auto& steve=scene.CreateGameObject("Stationary caster");steve.transform.position={-4,1,0};steve.AddComponent<MeshRenderer>();
        label=&scene.CreateGameObject("Presentation status");label->AddComponent<UIText>();
        demo->source=lamp->GetID();demo->steve=steve.GetID();demo->cube=cube->GetID();demo->status=label->GetID();
        for(int i=0;i<3;++i){
            auto& ball=scene.CreateGameObject("Ray marker "+std::to_string(i));auto& mesh=ball.AddComponent<MeshRenderer>();mesh.shape=int(BasicShape::Sphere);mesh.castsShadow=false;
            auto& config=ball.AddComponent<PresentationBall>();config.direction={0,-1,0};config.maxDistance=8;config.delaySeconds=.1f+.5f*i;config.speed=1;config.diameter=.1f;config.repeatSeconds=5;
            balls.push_back(&ball);
        }
    }
    void Step(float dt){scene.Update(dt);scene.LateUpdate(dt);}
    float Distance()const{auto distance=demo->BallDistance(balls.front()->GetID());Check(bool(distance),"Controller exposes calculated ray endpoint");return *distance;}
    size_t Visible()const{size_t count=0;for(auto* ball:balls)count+=ball->IsActive()&&ball->GetComponent<MeshRenderer>()->enabled;return count;}
};

void CheckEmissionAndDependencies(){
    Fixture fixture;fixture.Step(0);
    Check(fixture.Visible()==0&&fixture.demo->GetStats().visibleBalls==0,"Play begins with no prefilled balls");
    const auto firstBuild=fixture.demo->GetStats().pathRebuilds;
    const auto firstDistance=fixture.Distance();
    Check(firstBuild>0&&firstDistance>2.2f&&firstDistance<2.6f,"Initial downward ray stops at the cube top");
    fixture.Step(.2f);
    Check(fixture.Visible()==1&&fixture.demo->GetStats().visibleBalls==1,"First ball appears only after its emission delay");
    auto firstPosition=fixture.balls[0]->transform.position;
    fixture.Step(.5f);
    Check(fixture.Visible()==2,"Later ball emits sequentially instead of prefilled stream");
    Check(fixture.balls[0]->transform.position.y<firstPosition.y,"Emitted ball travels outward from the source");
    Check(fixture.demo->GetStats().pathRebuilds==firstBuild&&fixture.demo->GetStats().rayQueries==0,"Particle motion alone reuses calculated ray paths");

    fixture.cube->transform.position.x=3;fixture.Step(0);
    Check(fixture.demo->GetStats().pathRebuilds>firstBuild&&fixture.demo->GetStats().rayQueries>0,"Manual cube motion triggers actual ray intersection work");
    Check(fixture.Distance()>firstDistance+1,"Ray endpoint moves to ground after cube leaves the path");
    auto movedBuild=fixture.demo->GetStats().pathRebuilds;fixture.Step(.1f);
    Check(fixture.demo->GetStats().pathRebuilds==movedBuild&&fixture.demo->GetStats().rayQueries==0,"Moved cube held still returns to path reuse");
    fixture.cube->transform.position.x=0;fixture.Step(0);
    Check(std::abs(fixture.Distance()-firstDistance)<1e-4f,"Returning cube restores the original occlusion endpoint");
    fixture.lamp->transform.position.y=5;fixture.Step(0);
    Check(fixture.Distance()>firstDistance+.8f,"Moving source recomputes travel from its live position");
}

void CheckAutomaticSequence(){
    Fixture fixture(true);fixture.Step(0);const auto initial=fixture.cube->transform.position;
    for(int i=0;i<5;++i)fixture.Step(.1f);
    Check(fixture.cube->transform.position==initial,"Warmup keeps the held cube still while emission begins");
    for(int i=0;i<10;++i)fixture.Step(.1f);
    Check(fixture.cube->transform.position.x>initial.x+.5f&&fixture.cube->transform.position.x<initial.x+2.5f,"Automatic sequence moves cube between its two positions");
    for(int i=0;i<10;++i)fixture.Step(.1f);
    const auto held=fixture.cube->transform.position;fixture.Step(.1f);
    Check(glm::length(fixture.cube->transform.position-held)<1e-5f,"Automatic sequence includes a stationary hold for cache explanation");
    for(int i=0;i<19;++i)fixture.Step(.1f);
    Check(glm::length(fixture.cube->transform.position-initial)<1e-4f,"Automatic sequence returns cube to its starting pose");
    fixture.demo->autoMotion=false;fixture.cube->transform.position={2,1.5f,.2f};fixture.Step(.2f);
    Check(fixture.cube->transform.position==Vector3(2,1.5f,.2f),"Disabling automatic motion preserves presenter edits");
}

void CheckRenderTelemetry(){
    Fixture fixture;fixture.Step(0);
    SceneRenderer renderer;renderer.SetShadowResolution(128);ViewportTarget target;
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({5,5,7});camera.LookAtObj({0,1,0});
    auto render=[&]{target.Render(128,128,[&]{renderer.Render(fixture.scene,camera);});ObservePresentation(fixture.scene,renderer);Check(glGetError()==GL_NO_ERROR,"Presentation rendering leaves no GL errors");};
    render();Check(renderer.GetShadowStats().faces==6&&fixture.demo->GetStats().shadowFaces==6,"Initial telemetry reports actual point map creation");
    fixture.Step(.2f);render();
    Check(renderer.GetShadowStats().faces==0&&fixture.demo->GetStats().shadowFaces==0,"Emitting noncasting balls preserves actual shadow cache");
    fixture.cube->transform.position.x=3;fixture.Step(0);render();
    Check(renderer.GetShadowStats().faces==6&&fixture.demo->GetStats().shadowFaces==6,"Cube motion reports actual six-face rebuild");
    Check(fixture.demo->GetStats().shadowUpdates==1&&fixture.demo->GetStats().changedCasters==1&&fixture.demo->GetStats().affectedLights==1,"Telemetry follows actual renderer invalidation counters");
    fixture.Step(.1f);render();
    Check(renderer.GetShadowStats().faces==0&&fixture.demo->GetStats().shadowFaces==0,"Stationary frame after motion restores cached shadows");
    Check(!fixture.label->GetComponent<UIText>()->text.empty(),"Presenter status is exposed to the Game UI");
}
}
int main(){GLFWwindow* window=nullptr;try{
    Debug::Init();CheckEmissionAndDependencies();CheckAutomaticSequence();
    Check(glfwInit()!=0,"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(128,128,"Presentation regression",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");RenderCommand::SetDepthTestEnabled(true);
    CheckRenderTelemetry();glfwDestroyWindow(window);glfwTerminate();std::cout<<"Progressive emission, live ray endpoints, automatic holds and real shadow telemetry passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
