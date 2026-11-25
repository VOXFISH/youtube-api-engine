#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "Scene/Components.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <iostream>
#include <string>

using namespace Rice;
namespace {
constexpr int Size=256;
using Image=std::vector<unsigned char>;
size_t comparisons=0;
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void InvalidationCpuFallback(){
    ShadowInvalidation cache;
    std::vector<ShadowLight> lights(3);
    for(size_t i=0;i<lights.size();++i){lights[i].id=EntityID(100+i);lights[i].position={float(i)*24.f,4,0};lights[i].range=8;}
    lights[2].type=1;
    const SpatialBounds valid{Vector3(-.5f),Vector3(.5f)};
    auto snapshot=[](SpatialBounds bounds,uint64_t token){
        ShadowInvalidation::Snapshot value;value.data={token};value.casters.push_back({1,bounds,0,1,0,0});return value;
    };
    cache.Update(1,snapshot(valid,1),lights);auto before=cache.Revisions();
    Check(cache.GetStats().affectedLights==3,"First CPU snapshot invalidates every light");
    cache.Update(1,snapshot(valid,2),lights);
    Check(cache.Revisions()[0]==before[0]+1&&cache.Revisions()[1]==before[1]&&cache.Revisions()[2]==before[2]+1,"Local change invalidates point and directional dependencies only");
    Check(cache.GetStats().bvhBuilds==1,"Local dependency query uses the shared BVH");
    auto checkAll=[&](const std::array<uint64_t,8>& previous,const char* label){
        for(size_t i=0;i<lights.size();++i)Check(cache.Revisions()[i]==previous[i]+1,label);
        Check(cache.GetStats().affectedLights==lights.size(),label);
        Check(cache.GetStats().bvhBuilds==0,"Invalid bounds or mode transitions bypass dirty BVH construction");
    };
    SpatialBounds invalid=valid;invalid.low.x=std::numeric_limits<float>::quiet_NaN();
    before=cache.Revisions();cache.Update(1,snapshot(invalid,3),lights);checkAll(before,"Nonfinite current bounds conservatively invalidate all lights");
    before=cache.Revisions();cache.Update(1,snapshot(valid,4),lights);checkAll(before,"Nonfinite previous bounds conservatively invalidate all lights");
    invalid={Vector3(2),Vector3(-2)};
    before=cache.Revisions();cache.Update(1,snapshot(invalid,5),lights);checkAll(before,"Inverted bounds conservatively invalidate all lights");
    before=cache.Revisions();cache.Update(1,snapshot(valid,6),lights);checkAll(before,"Recovery from inverted bounds is conservative");
    before=cache.Revisions();cache.SetSelectiveEnabled(false);cache.Update(1,snapshot(valid,6),lights);checkAll(before,"Switching to global mode invalidates history safely");
    before=cache.Revisions();cache.SetSelectiveEnabled(true);cache.Update(1,snapshot(valid,6),lights);checkAll(before,"Returning to selective mode invalidates history safely");
    before=cache.Revisions();cache.Update(1,snapshot(valid,6),lights);
    Check(cache.Revisions()==before&&!cache.Changed(),"Unchanged snapshot after mode transition is cached");
}
Image Capture(SceneRenderer& renderer,ViewportTarget& target,Scene& scene,const Camera& camera){
    Image image(Size*Size*4);
    target.Render(Size,Size,[&]{renderer.Render(scene,camera);glReadPixels(0,0,Size,Size,GL_RGBA,GL_UNSIGNED_BYTE,image.data());});
    Check(glGetError()==GL_NO_ERROR,"Shadow invalidation capture leaves no OpenGL errors");return image;
}
void Equivalent(const Image& expected,const Image& actual,const std::string& label){
    int largest=0;size_t differing=0;
    for(size_t i=0;i<expected.size();++i){int delta=std::abs(int(expected[i])-int(actual[i]));largest=std::max(largest,delta);if(delta>1)++differing;}
    Check(differing==0,label+": image differs in "+std::to_string(differing)+" channels, max error "+std::to_string(largest));++comparisons;
}
PerspectiveCamera GroundCamera(){PerspectiveCamera camera(45,1,.1f,150);camera.SetPosition({5,5,7});camera.LookAtObj({0,.3f,0});return camera;}
GameObject& Point(Scene& scene,Vector3 position,float range=8){
    auto& object=scene.CreateGameObject("Point light");object.transform.position=position;
    auto& light=object.AddComponent<Light>();light.range=range;light.intensity=2;return object;
}
GameObject& Cube(Scene& scene,Vector3 position){
    auto& object=scene.CreateGameObject("Caster");object.transform.position=position;object.AddComponent<MeshRenderer>().color={.6f,.25f,.1f,1};return object;
}
size_t AllFaces(const Scene& scene){
    size_t faces=scene.settings.directionalLight?1:0,slots=faces;
    for(const auto& object:scene.GetObjects())if(object->IsActive())if(const auto* light=object->GetComponent<Light>();light&&light->enabled&&slots<8){
        ++slots;if(light->castsShadow)faces+=light->type==1?1:6;
    }
    return faces;
}
// Three renderers retain separate histories throughout each mutation sequence.
// In particular, the full redraw never primes the selective renderer's maps.
struct Compare {
    SceneRenderer selective,global,forced;
    ViewportTarget target;
    Compare(){
        global.SetShadowSelectiveInvalidationEnabled(false);forced.SetShadowCachingEnabled(false);
        for(auto* renderer:{&selective,&global,&forced})renderer->SetShadowResolution(256);
    }
    Image Step(Scene& scene,const Camera& camera,const std::string& label,size_t expectedFaces,int expectedGlobal=-1,int expectedReflection=-1){
        auto actual=Capture(selective,target,scene,camera);
        Check(selective.GetShadowStats().faces==expectedFaces,label+": selective depth faces "+std::to_string(selective.GetShadowStats().faces)+" != "+std::to_string(expectedFaces));
        if(expectedReflection>=0)Check(selective.GetSpatialStats().captureFaces==size_t(expectedReflection),label+": selective reflection faces");
        auto previous=Capture(global,target,scene,camera);
        if(expectedGlobal>=0)Check(global.GetShadowStats().faces==size_t(expectedGlobal),label+": global depth faces");
        auto reference=Capture(forced,target,scene,camera);
        Check(forced.GetShadowStats().faces==AllFaces(scene),label+": forced reference renders all active depth faces");
        Equivalent(reference,actual,label+" selective/forced");Equivalent(reference,previous,label+" global/forced");return actual;
    }
};

void MotionAndMembership(){
    Scene scene;scene.settings.ground=true;
    for(float x:{0.f,24.f,48.f}){Point(scene,{x,4,0});if(x!=0)Cube(scene,{x+3,1.5f,2});}
    auto& mover=Cube(scene,{0,1.5f,0});auto& remote=Cube(scene,{100,1.5f,0});
    auto camera=GroundCamera();Compare compare;
    compare.Step(scene,camera,"initial separated points",18,18);
    compare.Step(scene,camera,"unchanged separated points",0,0);
    mover.transform.position.x=1;compare.Step(scene,camera,"local motion refreshes only one point",6,18);
    camera.SetPosition({6,5,7});camera.LookAtObj({0,.3f,0});compare.Step(scene,camera,"camera motion preserves maps",0,0);
    remote.transform.position.x=101;compare.Step(scene,camera,"motion outside every point volume",0,18);
    mover.transform.position.x=48;compare.Step(scene,camera,"teleport checks old and new bounds, not the intervening point",12,18);
    camera.SetPosition({53,5,7});camera.LookAtObj({48,.3f,0});compare.Step(scene,camera,"destination shadow is correct without another refresh",0,0);
    mover.transform.position.x=90;compare.Step(scene,camera,"leaving point volume clears old shadow",6,18);
    camera.SetPosition({29,5,7});camera.LookAtObj({24,.3f,0});compare.Step(scene,camera,"camera visits next point before caster entry",0,0);
    mover.transform.position.x=24;compare.Step(scene,camera,"entering point volume refreshes new shadow",6,18);
    auto& mesh=*mover.GetComponent<MeshRenderer>();
    mesh.castsShadow=false;compare.Step(scene,camera,"cast flag removal retains previous bounds",6,18);
    mesh.castsShadow=true;compare.Step(scene,camera,"cast flag addition uses current bounds",6,18);
    mesh.color.a=0;compare.Step(scene,camera,"alpha removes caster",6,18);
    mesh.color.a=1;compare.Step(scene,camera,"alpha restores caster",6,18);
    mesh.enabled=false;compare.Step(scene,camera,"renderer disable retains previous bounds",6,18);
    mesh.enabled=true;compare.Step(scene,camera,"renderer enable uses current bounds",6,18);
    mover.transform.localScale=Vector3(0);compare.Step(scene,camera,"zero scale clears old depth",6,18);
    mover.transform.localScale=Vector3(1);compare.Step(scene,camera,"nonzero scale restores depth",6,18);
    mover.SetActive(false);compare.Step(scene,camera,"inactive object clears old depth",6,18);
    mover.SetActive(true);compare.Step(scene,camera,"reactivated object restores depth",6,18);
    mover.RemoveComponent<MeshRenderer>();compare.Step(scene,camera,"removed component clears old depth",6,18);
    mover.AddComponent<MeshRenderer>();compare.Step(scene,camera,"new component restores depth",6,18);
    scene.Destroy(mover.GetID());scene.FlushDestroyed();compare.Step(scene,camera,"deleted object retains only safe old bounds",6,18);
    Cube(scene,{24,1.5f,0});compare.Step(scene,camera,"new object affects only its point",6,18);
    compare.Step(scene,camera,"membership changes settle back to cached",0,0);
}

class MaskTexture final:public Texture2D {
    GLuint texture=0;
public:
    MaskTexture(unsigned char left,unsigned char right){
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        const unsigned char pixels[]={255,255,255,left,255,255,255,right};
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    ~MaskTexture(){glDeleteTextures(1,&texture);}
    uint32_t GetWidth()const override{return 2;}uint32_t GetHeight()const override{return 1;}
    void Bind(uint32_t slot)const override{glActiveTexture(GL_TEXTURE0+slot);glBindTexture(GL_TEXTURE_2D,texture);}
};
void AppearanceAndGeometry(){
    Scene scene;scene.settings.ground=true;Point(scene,{0,4,0});Point(scene,{24,4,0});
    auto& object=scene.CreateGameObject("Masked sprite");object.transform.position={0,1.5f,0};object.transform.rotation.x=-90;object.transform.localScale=Vector3(2);
    auto& sprite=object.AddComponent<SpriteRenderer>();sprite.texture=CreateRef<MaskTexture>(0,255);sprite.uvRect={0,0,.49f,1};
    auto camera=GroundCamera();Compare compare;
    compare.Step(scene,camera,"masked sprite initial",12,12);
    sprite.uvRect={.51f,0,.49f,1};compare.Step(scene,camera,"UV edit with unchanged bounds",6,12);
    sprite.texture=CreateRef<MaskTexture>(0,0);compare.Step(scene,camera,"alpha texture replacement with unchanged bounds",6,12);
    sprite.texture=CreateRef<MaskTexture>(255,255);compare.Step(scene,camera,"replacement restores opaque sprite",6,12);
    sprite.color={.1f,.8f,.3f,1};compare.Step(scene,camera,"sprite RGB is not a depth dependency",0,0);
    sprite.color.a=0;compare.Step(scene,camera,"sprite alpha removal",6,12);
    sprite.color.a=1;compare.Step(scene,camera,"sprite alpha restoration",6,12);
    object.AddComponent<MeshRenderer>();compare.Step(scene,camera,"mesh plus sprite component on same entry",6,12);
    object.RemoveComponent<SpriteRenderer>();object.transform.rotation=Vector3(0);compare.Step(scene,camera,"remove only sprite component",6,12);
    auto& mesh=*object.GetComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));compare.Step(scene,camera,"custom primitive conversion",6,12);
    auto before=mesh.custom.Bounds();mesh.custom.positions[0].x*=.5f;mesh.custom.Touch();
    Check(mesh.custom.Bounds()==before,"Custom mutation intentionally preserves AABB");
    compare.Step(scene,camera,"custom topology revision with unchanged AABB",6,12);
    mesh.color={.2f,.6f,.9f,1};compare.Step(scene,camera,"mesh RGB is not a depth dependency",0,0);
    auto& material=object.AddComponent<Material>();material.albedoTexture=CreateRef<MaskTexture>(0,0);
    compare.Step(scene,camera,"base material alpha mask",6,12);
    material.enabled=false;compare.Step(scene,camera,"disabled material restores opaque depth",6,12);
    auto& override=mesh.triangleMaterials.faces[0];override.albedoTexture=CreateRef<MaskTexture>(0,0);
    compare.Step(scene,camera,"triangle mask override",6,12);
    override.albedoTexture=CreateRef<MaskTexture>(255,255);compare.Step(scene,camera,"triangle mask replacement",6,12);
    mesh.triangleMaterials.faces.clear();compare.Step(scene,camera,"triangle mask removal",6,12);
    mesh.shape=int(BasicShape::Sphere);compare.Step(scene,camera,"primitive shape changes with same bounds",6,12);
    compare.Step(scene,camera,"appearance mutations settle",0,0);
}

void ParentsAndCaptureVolume(){
    Scene scene;scene.settings.ground=true;Point(scene,{0,4,0});Point(scene,{24,4,0});
    auto& parent=scene.CreateGameObject("Parent");auto& child=Cube(scene,{0,1.5f,0});child.SetParent(&parent);
    auto camera=GroundCamera();Compare compare;compare.Step(scene,camera,"parent initial",12,12);
    parent.transform.position.x=1;compare.Step(scene,camera,"parent motion changes child world bounds",6,12);
    parent.transform.rotation.y=90;compare.Step(scene,camera,"parent rotation changes child world matrix",6,12);
    parent.SetActive(false);compare.Step(scene,camera,"parent deactivation clears descendant",6,12);
    parent.SetActive(true);compare.Step(scene,camera,"parent reactivation restores descendant",6,12);
    auto& corner=Cube(scene,{7,4,7});compare.Step(scene,camera,"point capture cube corner outside radial range",6,12);
    corner.transform.position.y+=.2f;compare.Step(scene,camera,"capture cube is conservative for corner motion",6,12);
    corner.transform.position={7,4,9};compare.Step(scene,camera,"old cube corner remains an invalidation source",6,12);
    corner.transform.position.z=10;compare.Step(scene,camera,"fully outside capture cube is cached",0,12);
}

void LightsDirectionalAndSceneSwitch(){
    Scene scene;scene.settings.ground=true;auto& first=Point(scene,{0,4,0});Point(scene,{24,4,0});
    auto& caster=Cube(scene,{0,1.5f,0});auto camera=GroundCamera();Compare compare;
    compare.Step(scene,camera,"light mutation initial",12,12);
    first.transform.position.z=1;compare.Step(scene,camera,"one point position edit",6,6);
    auto& light=*first.GetComponent<Light>();light.range=9;compare.Step(scene,camera,"one point range edit",6,6);
    light.intensity=3;light.color={.9f,.8f,1,1};compare.Step(scene,camera,"radiance edits preserve depth",0,0);
    light.castsShadow=false;compare.Step(scene,camera,"shadow layer removal",6,6);
    light.castsShadow=true;compare.Step(scene,camera,"shadow layer restoration",12,12);
    light.enabled=false;compare.Step(scene,camera,"light slot removal",6,6);
    light.enabled=true;compare.Step(scene,camera,"light slot restoration",12,12);
    light.type=1;light.direction={1,-2,0};compare.Step(scene,camera,"point changes to directional",7,7);
    caster.transform.position.x=1;compare.Step(scene,camera,"directional refreshes globally while remote point is cached",1,7);
    auto& noncaster=Cube(scene,{100,2,0});noncaster.GetComponent<MeshRenderer>()->castsShadow=false;
    compare.Step(scene,camera,"noncaster expands directional fitting bounds",1);
    noncaster.transform.position.x=110;compare.Step(scene,camera,"noncaster fitting motion updates directional matrix",1,1);
    for(auto* renderer:{&compare.selective,&compare.global,&compare.forced})renderer->SetShadowResolution(128);
    compare.Step(scene,camera,"resolution change redraws every layer",7,7);
    compare.Step(scene,camera,"resized maps settle",0,0);
    scene.Destroy(first.GetID());scene.FlushDestroyed();compare.Step(scene,camera,"leading light deletion remaps layers",6,6);
    Scene replacement;replacement.settings.ground=true;Point(replacement,{0,4,0});Point(replacement,{24,4,0});Cube(replacement,{0,1.5f,0});
    compare.Step(replacement,camera,"scene replacement resets dependency histories",12,12);
    compare.Step(replacement,camera,"replacement scene settles",0,0);
}

void FrozenReflection(){
    Scene scene;scene.settings.ground=true;
    auto& mirror=scene.CreateGameObject("Frozen mirror");mirror.transform.position={0,2,0};mirror.transform.localScale={4,4,1};
    mirror.AddComponent<MeshRenderer>().color=Color(1);auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.045f;
    auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=128;probe.maxObjectSize=10000;
    Point(scene,{.8f,4,3.5f},10);Point(scene,{30,4,0},8);
    auto& caster=Cube(scene,{.8f,1.5f,3.5f});auto& remote=Cube(scene,{100,1.5f,0});
    PerspectiveCamera camera(45,1,.1f,150);camera.SetPosition({0,2,5});camera.LookAtObj({0,1,0});Compare compare;
    compare.Step(scene,camera,"frozen reflection initial",12,12,6);
    compare.Step(scene,camera,"frozen reflection stable",0,0,0);
    caster.transform.position.x+=2;compare.Step(scene,camera,"local shadow mutation refreshes frozen probe once",6,12,6);
    compare.Step(scene,camera,"updated frozen reflection settles",0,0,0);
    remote.transform.position.x+=1;compare.Step(scene,camera,"remote caster mutation still refreshes reflection geometry",0,12,6);
    compare.Step(scene,camera,"remote mutation settles",0,0,0);
}
}
int main(){GLFWwindow* window=nullptr;try{
    Debug::Init();InvalidationCpuFallback();Check(glfwInit()!=0,"GLFW init");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(Size,Size,"Selective shadow invalidation regression",nullptr,nullptr);Check(window!=nullptr,"Hidden OpenGL context");
    glfwMakeContextCurrent(window);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
    RenderCommand::SetDepthTestEnabled(true);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);
    MotionAndMembership();AppearanceAndGeometry();ParentsAndCaptureVolume();LightsDirectionalAndSceneSwitch();FrozenReflection();
    glfwDestroyWindow(window);glfwTerminate();std::cout<<"Selective/global/forced shadow histories passed: "<<comparisons<<" image comparisons\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
