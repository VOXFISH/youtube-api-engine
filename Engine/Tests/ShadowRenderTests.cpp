#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Rice/Debug.h"
#include "Scene/Components.h"
#include "Project/SceneCodec.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <fstream>
#include <iostream>

using namespace Rice;
namespace {
constexpr int Size=256;
using Image=std::vector<unsigned char>;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Image Capture(SceneRenderer& renderer,ViewportTarget& target,Scene& scene,const Camera& camera){
    Image pixels(Size*Size*4);
    target.Render(Size,Size,[&]{renderer.Render(scene,camera);glReadPixels(0,0,Size,Size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});
    Check(glGetError()==GL_NO_ERROR,"Shadow rendering leaves OpenGL errors");return pixels;
}
float Sample(const Image& image,const Camera& camera,Vector3 position,int radius=1){
    Vector4 clip=camera.GetVPMatrix()*Vector4(position,1);
    Check(clip.w>0,"Shadow sample lies in front of camera");
    Vector2 pixel=(Vector2(clip)/clip.w+Vector2(1))*(Size*.5f);
    int x=int(pixel.x),y=int(pixel.y);Check(x>radius&&x<Size-radius&&y>radius&&y<Size-radius,"Shadow sample lies inside viewport");
    float total=0;int count=0;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
        size_t offset=(size_t(y+dy)*Size+size_t(x+dx))*4;
        total+=(image[offset]+image[offset+1]+image[offset+2])/3.f;++count;
    }return total/count;
}
void CheckDarker(const Image& clear,const Image& shadow,const Camera& camera,Vector3 point,const char* message,float difference=20){
    float lit=Sample(clear,camera,point),dark=Sample(shadow,camera,point);
    std::cout<<message<<": "<<lit<<" -> "<<dark<<'\n';Check(lit>dark+difference,message);
}
GameObject& Caster(Scene& scene){
    auto& object=scene.CreateGameObject("Shadow caster");object.transform.position={0,1.5f,0};object.AddComponent<MeshRenderer>().color={.6f,.25f,.1f,1};return object;
}
PerspectiveCamera GroundCamera(){PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({5,5,7});camera.LookAtObj({0,.3f,0});return camera;}

void CheckSubtexelShadowEdge(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;
    scene.settings.ground=true;
    scene.settings.directionalLight=true;
    scene.settings.lightDirection={1,-2,0};
    Caster(scene);
    // Magnify a straight edge on the neutral ground so one map texel spans
    // many screen pixels. Snapped, equal-weight 3x3 PCF gives only four tones.
    PerspectiveCamera camera(8,1,.1f,10);
    camera.SetPosition({1.5f,2,1});
    camera.LookAtObj({1.5f,0,0});
    renderer.SetShadowResolution(64);
    const auto image=Capture(renderer,target,scene,camera);
    std::array<bool,256> tones{};
    for(int x=0;x<Size;++x)tones[image[4*(Size*(Size/2)+x)]]=true;
    Check(std::count(tones.begin(),tones.end(),true)>=16,
          "Magnified shadow edges vary continuously within each map texel");
    renderer.SetShadowResolution(512);
}

void CheckObjectSelfShadow(SceneRenderer& renderer,ViewportTarget& target){
    for(bool directional:{true,false}){
        Scene scene;
        scene.settings.directionalLight=directional;
        scene.settings.lightDirection={0,-1,0};
        if(!directional){
            auto& lamp=scene.CreateGameObject("Point light");
            lamp.transform.position={0,5,0};
            auto& light=lamp.AddComponent<Light>();light.range=12;light.intensity=3;
        }

        // All three shelves belong to one mesh. From the side the camera sees
        // the bottom shelf. From above the light sees both upper shelves first.
        const auto& camera=GroundCamera();
        const Vector3 bottom{0,0,0};
        auto& shelves=scene.CreateGameObject("Three shelves",(EntityID(1)<<40)+73);
        auto& mesh=shelves.AddComponent<MeshRenderer>();
        mesh.shape=int(BasicShape::Custom);mesh.color=Color(1);
        auto addShelf=[&](float height,float radius){
            auto& shape=mesh.custom;
            const auto offset=static_cast<uint32_t>(shape.positions.size());
            shape.positions.insert(shape.positions.end(),{
                {-radius,height,-radius},{-radius,height,radius},{radius,height,radius},{radius,height,-radius}
            });
            for(uint32_t index:{0u,1u,2u,0u,2u,3u})shape.indices.push_back(offset+index);
            shape.uv.resize(shape.indices.size());
        };
        addShelf(0,3);addShelf(1.2f,.7f);addShelf(2,.7f);
        renderer.SetShadowsEnabled(false);const auto clear=Capture(renderer,target,scene,camera);
        renderer.SetShadowsEnabled(true);const auto alone=Capture(renderer,target,scene,camera);
        const float lit=Sample(clear,camera,bottom);
        Check(lit>60,"The bottom shelf is visible and directly illuminated");
        Check(std::abs(lit-Sample(alone,camera,bottom))<3,
              "Multiple surfaces of the receiving GameObject must not shadow each other");

        // A distinct entity behind the light's two nearest surfaces must still
        // cast, even when parented to the receiver and sharing its low ID bits.
        auto& blocker=scene.CreateGameObject("Independent blocker",(EntityID(2)<<40)+73);
        blocker.SetParent(&shelves);
        blocker.transform.position={0,.6f,0};blocker.transform.localScale=Vector3(.55f);
        blocker.AddComponent<MeshRenderer>();
        const auto blocked=Capture(renderer,target,scene,camera);
        CheckDarker(alone,blocked,camera,bottom,"A different caster behind the receiver's own surfaces casts a shadow");
        Check(Capture(renderer,target,scene,camera)==blocked,"Cached shadows retain distinct caster ownership");
        scene.Destroy(blocker.GetID());
        const auto removed=Capture(renderer,target,scene,camera);
        Check(std::abs(lit-Sample(removed,camera,bottom))<3,"Removing the foreign caster clears the bottom shelf");

        auto& pedestal=scene.CreateGameObject("Separate cube receiver");
        pedestal.transform.position={0,.2f,0};pedestal.transform.localScale={2,.1f,2};
        pedestal.AddComponent<MeshRenderer>().color=Color(1);
        const auto onPedestal=Capture(renderer,target,scene,camera);
        mesh.castsShadow=false;
        const auto clearPedestal=Capture(renderer,target,scene,camera);
        CheckDarker(clearPedestal,onPedestal,camera,{0,.25f,0},
                    "The shelf mesh continues to cast onto an independent instanced cube");
        auto& cube=scene.CreateGameObject("Second cube in the same draw");
        cube.transform.position={0,1.f,0};cube.transform.localScale=Vector3(.5f);
        cube.AddComponent<MeshRenderer>();
        const auto instances=Capture(renderer,target,scene,camera);
        CheckDarker(clearPedestal,instances,camera,{0,.25f,0},
                    "Each batched cube casts onto the other cube independently");
    }
}

void CheckDirectional(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;scene.settings.ground=true;scene.settings.directionalLight=true;scene.settings.lightDirection={1,-2,0};
    auto& object=Caster(scene);auto camera=GroundCamera();
    renderer.SetShadowsEnabled(false);auto clear=Capture(renderer,target,scene,camera);
    renderer.SetShadowsEnabled(true);auto shadow=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==1&&renderer.GetShadowStats().faces==1,"Default directional light renders one shadow view");
    CheckDarker(clear,shadow,camera,{1.1f,0,0},"Default ground receives directional shadow");
    Check(std::abs(Sample(clear,camera,{-2,0,0})-Sample(shadow,camera,{-2,0,0}))<3,"Unoccluded ground retains its appearance");
    auto repeated=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==0&&renderer.GetShadowStats().faces==0,"Static directional shadow reuses its depth map");
    Check(repeated==shadow,"Cached directional shadow is pixel-identical");
    scene.settings.lightColor={.8f,.9f,1,1};scene.settings.lightIntensity=1.5f;Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==0,"Light radiance change does not regenerate depth");
    object.transform.position.x+=2;auto moved=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==1,"Direct caster transform edit refreshes shadow");
    Check(Sample(moved,camera,{1.1f,0,0})>Sample(shadow,camera,{1.1f,0,0})+20,"Moving caster removes the old shadow");
    scene.settings.lightDirection={-1,-2,0};Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==1,"Changing directional light direction refreshes shadow");
    scene.settings.directionalLight=false;auto disabled=Capture(renderer,target,scene,camera);
    Check(Sample(disabled,camera,{1.1f,0,0})>170,"Disabling light removes its ground shadow");
}

void CheckPointAndMesh(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;scene.settings.ground=true;auto& object=Caster(scene);
    auto& lamp=scene.CreateGameObject("Point light");lamp.transform.position={0,4,0};auto& light=lamp.AddComponent<Light>();light.range=10;light.intensity=2;
    auto camera=GroundCamera();renderer.SetShadowsEnabled(false);auto clear=Capture(renderer,target,scene,camera);
    renderer.SetShadowsEnabled(true);auto shadow=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==1&&renderer.GetShadowStats().faces==6,"Point light renders six shadow views");
    CheckDarker(clear,shadow,camera,{.7f,0,.3f},"Default ground receives point shadow");
    Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==0,"Static point shadow reuses six faces");
    lamp.transform.position.x=1;Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==6,"Point light translation refreshes all six faces");
    lamp.transform.position.x=0;
    scene.settings.ground=false;auto& floor=scene.CreateGameObject("Mesh floor");floor.transform.rotation.x=-90;floor.transform.localScale={10,10,1};floor.AddComponent<MeshRenderer>().shape=int(BasicShape::Rectangle);
    renderer.SetShadowsEnabled(false);clear=Capture(renderer,target,scene,camera);renderer.SetShadowsEnabled(true);shadow=Capture(renderer,target,scene,camera);
    CheckDarker(clear,shadow,camera,{.7f,0,.3f},"Mesh receives point shadow");
    object.GetComponent<MeshRenderer>()->SetShape(int(BasicShape::Custom));Capture(renderer,target,scene,camera);
    auto& mesh=*object.GetComponent<MeshRenderer>();mesh.custom.positions[0].x-=.2f;mesh.custom.Touch();Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==1,"Custom mesh revision refreshes depth map");
    light.castsShadow=false;auto noShadow=Capture(renderer,target,scene,camera);
    CheckDarker(noShadow,shadow,camera,{.7f,0,.3f},"Per-light shadow switch preserves illumination");
    light.castsShadow=true;Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==6,"Re-enabling point shadows refreshes six faces");
    light.enabled=false;Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==0,"Disabled light generates no shadow views");
}

class AlphaTexture final:public Texture2D {
    GLuint texture=0;
public:
    explicit AlphaTexture(unsigned char alpha){
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);unsigned char rgba[]={255,255,255,alpha};
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    ~AlphaTexture(){glDeleteTextures(1,&texture);}
    uint32_t GetWidth()const override{return 1;}uint32_t GetHeight()const override{return 1;}
    void Bind(uint32_t slot)const override{glActiveTexture(GL_TEXTURE0+slot);glBindTexture(GL_TEXTURE_2D,texture);}
};
void CheckAlphaCaster(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;scene.settings.ground=true;
    auto& lamp=scene.CreateGameObject();lamp.transform.position={0,4,0};auto& light=lamp.AddComponent<Light>();light.range=10;light.intensity=2;
    auto& object=scene.CreateGameObject("Cutout caster");object.transform.position={0,1.5f,0};object.transform.rotation.x=-90;
    auto& sprite=object.AddComponent<SpriteRenderer>();sprite.texture=CreateRef<AlphaTexture>(0);
    auto camera=GroundCamera();auto transparent=Capture(renderer,target,scene,camera);
    sprite.texture=CreateRef<AlphaTexture>(255);auto opaque=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==6,"Caster texture change invalidates shadow cache");
    CheckDarker(transparent,opaque,camera,{.65f,0,.15f},"Sprite alpha discard is respected by depth pass");
    sprite.enabled=false;auto disabled=Capture(renderer,target,scene,camera);
    Check(Sample(disabled,camera,{.65f,0,.15f})>Sample(opaque,camera,{.65f,0,.15f})+20,"Disabled caster no longer casts shadow");
}

void CheckPointFaces(SceneRenderer& renderer,ViewportTarget& target){
    const Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(auto direction:directions){
        Scene scene;auto& light=scene.CreateGameObject("Omnidirectional light").AddComponent<Light>();light.range=8;light.intensity=2;
        Vector3 normal=-direction,u=std::abs(normal.y)>.9f?Vector3(1,0,0):glm::normalize(glm::cross(Vector3(0,1,0),normal));
        Vector3 v=glm::cross(normal,u);Matrix4 matrix(1);matrix[0]=Vector4(u*2.f,0);matrix[1]=Vector4(v*2.f,0);matrix[2]=Vector4(normal,0);matrix[3]=Vector4(direction*4.f,1);
        auto& receiver=scene.CreateGameObject("Face receiver");receiver.transform.SetMatrix(matrix);auto& mesh=receiver.AddComponent<MeshRenderer>();mesh.shape=int(BasicShape::Rectangle);mesh.color=Color(1);
        auto& caster=scene.CreateGameObject("Face occluder");caster.transform.position=direction*2.f+u*.3f;caster.transform.localScale=Vector3(.5f);caster.AddComponent<MeshRenderer>();
        auto point=direction*4.f+u*.6f;
        PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition(direction*2.5f+u*3.f+v*.4f);camera.LookAtObj(point);
        renderer.SetShadowsEnabled(false);auto clear=Capture(renderer,target,scene,camera);renderer.SetShadowsEnabled(true);auto shadow=Capture(renderer,target,scene,camera);
        CheckDarker(clear,shadow,camera,point,"Each point-shadow cube face shades its receiver");
    }
}

void CheckReflection(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;
    auto& mirror=scene.CreateGameObject("Mirror");mirror.transform.localScale={4,4,1};mirror.AddComponent<MeshRenderer>().color=Color(1);
    auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.045f;
    auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=256;probe.maxObjectSize=0;
    auto& receiver=scene.CreateGameObject("Reflected receiver");receiver.transform.position={.8f,0,3.5f};receiver.transform.rotation.y=180;receiver.transform.localScale={.6f,.6f,1};
    auto& surface=receiver.AddComponent<MeshRenderer>();surface.shape=int(BasicShape::Rectangle);surface.color=Color(1);
    auto& lamp=scene.CreateGameObject("Reflection light");lamp.transform.position={1.8f,0,2};auto& light=lamp.AddComponent<Light>();light.range=10;light.intensity=2;
    auto& blocker=scene.CreateGameObject("Reflection occluder");blocker.transform.position={1.3f,0,2.75f};blocker.transform.localScale=Vector3(.4f);blocker.AddComponent<MeshRenderer>();
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({0,0,5});camera.LookAtObj({0,0,0});
    renderer.SetShadowsEnabled(false);auto clear=Capture(renderer,target,scene,camera);renderer.SetShadowsEnabled(true);auto shadow=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==6,"Reflection capture does not regenerate shadows per cube face");
    // Reflect target across mirror front z=.5. This sample is on the mirror,
    // away from the directly rendered source and the light's blocker.
    CheckDarker(clear,shadow,camera,{.8f,0,-2.5f},"Cubemap reflection contains the receiver's shadow",12);
    Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==0,"Recapturing reflection reuses unchanged shadow maps");
    blocker.transform.position.x+=2;auto unblocked=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==6,"Moving reflected occluder refreshes depth once");
    CheckDarker(unblocked,shadow,camera,{.8f,0,-2.5f},"Reflected shadow updates after moving occluder",12);
}

void CheckMixedLights(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;scene.settings.ground=true;scene.settings.directionalLight=true;scene.settings.lightDirection={1,-2,0};
    auto& caster=Caster(scene);auto& lamp=scene.CreateGameObject("Additional point light");lamp.transform.position={-2,4,0};
    auto& point=lamp.AddComponent<Light>();point.range=10;point.intensity=0;
    auto camera=GroundCamera();Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==2&&renderer.GetShadowStats().faces==7,"Directional and point lights share seven depth layers");
    point.intensity=2;Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().updates==0,"Zero-to-positive intensity reuses valid depth maps");
    renderer.SetShadowResolution(256);Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==7,"Shadow resolution resize regenerates all active layers");
    Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==0,"Resized shadow maps are cached");
    renderer.SetShadowResolution(512);Capture(renderer,target,scene,camera);
    point.castsShadow=false;Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces<=1,"Disabling point shadow leaves only directional layer");
    Capture(renderer,target,scene,camera);Check(renderer.GetShadowStats().faces==0,"Disabled shadow state is stable");
    scene.settings.directionalLight=false;point.castsShadow=true;Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==6,"Light-list changes correctly remap point depth layers");
    scene.Destroy(caster.GetID());Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==6,"Removing a caster clears stale shadow depths");
    scene.Destroy(lamp.GetID());auto noLights=Capture(renderer,target,scene,camera);
    Check(renderer.GetShadowStats().faces==0,"Removing the final light leaves no shadow views");
    Check(Sample(noLights,camera,{.7f,0,.3f})>170,"Removed lights leave no stale ground shadow");
}

void WritePreview(const char* path,const Image& pixels){
    if(!path)return;
    std::ofstream file(path,std::ios::binary);Check(bool(file),"Cannot open shadow preview output");
    auto byte=[&](unsigned value){file.put(static_cast<char>(value));};
    auto word=[&](unsigned value){byte(value);byte(value>>8);};
    auto dword=[&](unsigned value){word(value);word(value>>16);};
    byte('B');byte('M');dword(54+Size*Size*3);dword(0);dword(54);
    dword(40);dword(Size);dword(Size);word(1);word(24);dword(0);dword(Size*Size*3);dword(0);dword(0);dword(0);dword(0);
    for(int y=0;y<Size;++y)for(int x=0;x<Size;++x){size_t p=(size_t(y)*Size+x)*4;byte(pixels[p+2]);byte(pixels[p+1]);byte(pixels[p]);}
    Check(bool(file),"Cannot write shadow preview output");
}
void CheckGroundReflection(SceneRenderer& renderer,ViewportTarget& target,const char* preview){
    Scene scene;scene.settings.ground=true;
    auto& mirror=scene.CreateGameObject("Ground mirror");mirror.transform.position={0,2,0};mirror.transform.localScale={4,4,1};mirror.AddComponent<MeshRenderer>().color=Color(1);
    auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.045f;
    auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=256;probe.maxObjectSize=0;
    auto& lamp=scene.CreateGameObject("Ground reflection light");lamp.transform.position={.8f,4,3.5f};auto& light=lamp.AddComponent<Light>();light.range=10;light.intensity=2;
    auto& caster=scene.CreateGameObject("Ground reflection caster");caster.transform.position={.8f,1.5f,3.5f};caster.AddComponent<MeshRenderer>().color={.8f,.3f,.1f,1};
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({0,2,5});camera.LookAtObj({0,1,0});
    renderer.SetShadowsEnabled(false);auto clear=Capture(renderer,target,scene,camera);renderer.SetShadowsEnabled(true);auto shadow=Capture(renderer,target,scene,camera);
    CheckDarker(clear,shadow,camera,{.8f,0,-2.5f},"Procedural ground shadow is visible inside cubemap reflection",12);
    Check(renderer.GetShadowStats().faces==6,"Ground reflection shares the existing six shadow views");
    WritePreview(preview,shadow);
}
void CheckOptimizationModes(SceneRenderer& renderer,ViewportTarget& target){
    auto equivalent=[](const Image& first,const Image& second){
        Check(first.size()==second.size(),"Shadow mode images have matching sizes");
        for(size_t i=0;i<first.size();++i)
            Check(std::abs(int(first[i])-int(second[i]))<=1,"Shadow culling/cache modes preserve the rendered image within one channel value");
    };
    for(bool directional:{true,false}){
        Scene scene;scene.settings.ground=true;scene.settings.directionalLight=directional;scene.settings.lightDirection={1,-2,0};
        if(!directional){
            auto& lamp=scene.CreateGameObject("Optimization point light");lamp.transform.position={0,4,0};
            auto& light=lamp.AddComponent<Light>();light.range=10;light.intensity=2;
        }
        for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){
            auto& object=scene.CreateGameObject("Optimization cube");object.transform.position={x*2.f,1.5f,z*2.f};object.transform.localScale=Vector3(.8f);
            object.AddComponent<MeshRenderer>().color={.6f,.25f,.1f,1};
            // A scene entry may contain two independently submitted caster shapes.
            if(x==0&&z==0)object.AddComponent<SpriteRenderer>();
        }
        auto& distant=scene.CreateGameObject("Outside point shadow range");distant.transform.position={30,1.5f,0};distant.AddComponent<MeshRenderer>();
        auto& noncaster=scene.CreateGameObject("Disabled shadow caster");noncaster.transform.position={-4,1.5f,0};noncaster.AddComponent<MeshRenderer>().castsShadow=false;
        auto& transparent=scene.CreateGameObject("Transparent shadow caster");transparent.transform.position={4,1.5f,0};transparent.AddComponent<MeshRenderer>().color.a=0;
        auto& collapsed=scene.CreateGameObject("Zero scale shadow caster");collapsed.transform.localScale=Vector3(0);collapsed.AddComponent<MeshRenderer>();
        const size_t faces=directional?1:6;
        auto camera=GroundCamera();renderer.SetShadowsEnabled(true);renderer.SetShadowCullingEnabled(false);renderer.SetShadowCachingEnabled(false);
        const auto reference=Capture(renderer,target,scene,camera);
        const auto unculled=renderer.GetShadowDrawStats();
        const size_t passes=2*faces;
        Check(unculled.candidates==11*passes&&unculled.submitted==11*passes,"Unculled counters count mesh and sprite submissions for both caster passes");
        Check(unculled.instances==10*passes&&unculled.drawCalls==2*passes,"Depth counters distinguish cube instances from actual GL draws in both passes");
        for(bool culling:{false,true})for(bool caching:{false,true}){
            renderer.SetShadowCullingEnabled(culling);
            // Prime each mode with a real depth render, regardless of the previous cache state.
            renderer.SetShadowCachingEnabled(false);const auto first=Capture(renderer,target,scene,camera);
            Check(renderer.GetShadowStats().updates==1&&renderer.GetShadowStats().faces==faces,"Forced shadow redraw updates exactly one directional or six point views");
            const auto drawn=renderer.GetShadowDrawStats();
            Check(drawn.candidates==unculled.candidates&&drawn.submitted<=drawn.candidates,"Culling preserves potential caster counts and never adds submissions");
            Check(drawn.instances<=drawn.submitted&&drawn.drawCalls>0,"Depth draw counters describe submitted geometry");
            if(culling&&!directional)Check(drawn.submitted<unculled.submitted,"Point BVH culling rejects out-of-range and other-face geometry");
            if(!culling)Check(drawn.submitted==unculled.submitted&&drawn.drawCalls==unculled.drawCalls&&drawn.instances==unculled.instances,"Disabling culling restores all caster submissions");
            equivalent(reference,first);
            renderer.SetShadowCachingEnabled(caching);const auto repeated=Capture(renderer,target,scene,camera);
            const auto repeatedDraws=renderer.GetShadowDrawStats();
            if(caching){
                Check(renderer.GetShadowStats().updates==0&&renderer.GetShadowStats().faces==0,"Static shadow caching skips every depth view with either culling mode");
                Check(repeatedDraws.candidates==0&&repeatedDraws.submitted==0&&repeatedDraws.drawCalls==0&&repeatedDraws.instances==0,"Cached shadow frames reset every draw counter to zero");
                Check(repeated==first,"Cached shadow output is pixel-identical to its depth-rendered frame");
            }else{
                Check(renderer.GetShadowStats().updates==1&&renderer.GetShadowStats().faces==faces,"Disabling caching redraws unchanged directional and point shadows");
                Check(repeatedDraws.candidates==drawn.candidates&&repeatedDraws.submitted==drawn.submitted&&repeatedDraws.drawCalls==drawn.drawCalls&&repeatedDraws.instances==drawn.instances,"Forced static redraw repeats the same measured depth work");
            }
            equivalent(reference,repeated);
        }
    }
    renderer.SetShadowCullingEnabled(true);renderer.SetShadowCachingEnabled(true);
}
void CheckSerialization(){
    ComponentRegistry registry;SceneCodec codec(registry);Scene scene;
    auto& object=scene.CreateGameObject("Saved shadow flags");
    object.AddComponent<MeshRenderer>().castsShadow=false;
    object.AddComponent<SpriteRenderer>().castsShadow=false;
    object.AddComponent<Light>().castsShadow=false;
    auto flags=[&](const GameObject& value,bool expected){
        return value.GetComponent<MeshRenderer>()->castsShadow==expected&&value.GetComponent<SpriteRenderer>()->castsShadow==expected&&value.GetComponent<Light>()->castsShadow==expected;
    };
    auto encoded=codec.Encode(scene);auto restored=codec.Decode(encoded);
    Check(flags(*restored->Find(object.GetID()),false),"Mesh, sprite and light shadow flags survive scene roundtrip");
    std::istringstream lines(encoded);std::ostringstream legacyScene;std::string line;
    while(std::getline(lines,line))if(line.find("\"Cast shadows\"")==std::string::npos)legacyScene<<line<<'\n';
    auto legacy=codec.Decode(legacyScene.str());
    Check(flags(*legacy->Find(object.GetID()),true),"Legacy scene without shadow flags defaults to casting");
    auto snapshots=ComponentSnapshot::CaptureAll(object,registry);
    auto& modernCopy=scene.CreateGameObject("Modern snapshot");ComponentSnapshot::Apply(modernCopy,snapshots,registry);
    Check(flags(modernCopy,false),"Component snapshots retain explicit disabled shadow flags");
    for(auto& snapshot:snapshots){
        Check(snapshot.data.ends_with("0\n"),"Shadow flag is the final optional snapshot property");
        snapshot.data.resize(snapshot.data.size()-2);
    }
    ComponentSnapshot::Apply(object,snapshots,registry);
    Check(flags(object,true),"Legacy snapshot missing final shadow flag resets existing components to default");
}
}
int main(int argc,char** argv){GLFWwindow* window=nullptr;try{
    Debug::Init();CheckSerialization();Check(glfwInit()!=0,"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(Size,Size,"Shadow regression",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
    {SceneRenderer renderer;ViewportTarget target;renderer.SetShadowResolution(512);RenderCommand::SetDepthTestEnabled(true);
        CheckSubtexelShadowEdge(renderer,target);CheckObjectSelfShadow(renderer,target);CheckDirectional(renderer,target);CheckPointAndMesh(renderer,target);CheckAlphaCaster(renderer,target);CheckPointFaces(renderer,target);CheckReflection(renderer,target);
        CheckMixedLights(renderer,target);CheckGroundReflection(renderer,target,argc>1?argv[1]:nullptr);CheckOptimizationModes(renderer,target);}
    glfwDestroyWindow(window);glfwTerminate();std::cout<<"Directional/point/ground/mesh/cutout/reflection shadow regression passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
