#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Scene/Components.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include <iostream>
#include <numeric>

using namespace Rice;

namespace {
constexpr int ImageSize=256;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

class AtlasTexture final:public Texture2D {
    GLuint texture=0;
public:
    explicit AtlasTexture(bool alternate=false){
        constexpr int width=48,height=8;
        std::vector<unsigned char> pixels(width*height*4);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            auto offset=(y*width+x)*4;
            int face=x/8,detail=(x+y)%3;
            pixels[offset]=static_cast<unsigned char>((alternate?150:30)+face*12+detail*10);
            pixels[offset+1]=static_cast<unsigned char>((alternate?40:100)+face*15+detail*10);
            pixels[offset+2]=static_cast<unsigned char>(30+face*10);
            // Per-face, asymmetric detail and cutouts exercise atlas coordinates and alpha testing.
            pixels[offset+3]=(x%8==1&&y==2)?0:255;
        }
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    ~AtlasTexture()override{glDeleteTextures(1,&texture);}
    uint32_t GetWidth()const override{return 48;}
    uint32_t GetHeight()const override{return 8;}
    void Bind(uint32_t slot)const override{glActiveTexture(GL_TEXTURE0+slot);glBindTexture(GL_TEXTURE_2D,texture);}
};

struct Capture {
    std::vector<unsigned char> pixels;
    uint32_t draws=0,batches=0,instances=0;
};

Capture Render(SceneRenderer& renderer,ViewportTarget& target,Scene& scene,const Camera& camera,bool instanced,EntityID excluded=0){
    renderer.SetCubeInstancingEnabled(instanced);
    Capture capture;capture.pixels.resize(ImageSize*ImageSize*4);
    target.Render(ImageSize,ImageSize,[&]{
        renderer.Render(scene,camera,false,excluded);
        auto stats=renderer.GetRenderStats();
        capture.draws=stats.drawCalls;capture.batches=stats.instancedDrawCalls;capture.instances=stats.instancedInstances;
        glReadPixels(0,0,ImageSize,ImageSize,GL_RGBA,GL_UNSIGNED_BYTE,capture.pixels.data());
    });
    Check(glGetError()==GL_NO_ERROR,"Instancing render leaves no OpenGL error");
    return capture;
}

void Equivalent(const Capture& before,const Capture& after,const char* label,size_t minimumVisible=500){
    Check(before.pixels.size()==after.pixels.size(),"Comparison image dimensions match");
    size_t visible=0,largeErrors=0;uint64_t absoluteError=0;int maximum=0;
    for(size_t p=0;p<before.pixels.size();p+=4){
        bool foreground=std::abs(int(before.pixels[p])-19)>4||std::abs(int(before.pixels[p+1])-20)>4||std::abs(int(before.pixels[p+2])-23)>4;
        if(foreground)++visible;
        int pixelError=0;
        for(int c=0;c<3;++c){int delta=std::abs(int(before.pixels[p+c])-int(after.pixels[p+c]));absoluteError+=delta;pixelError=std::max(pixelError,delta);}
        if(pixelError>2)++largeErrors;maximum=std::max(maximum,pixelError);
    }
    const double mean=double(absoluteError)/(ImageSize*ImageSize*3);
    std::cout<<label<<": visible="<<visible<<", max channel error="<<maximum<<", mean="<<mean<<", >2px="<<largeErrors<<'\n';
    Check(visible>=minimumVisible,"Pixel equivalence compares visible geometry, not empty frames");
    Check(largeErrors<=size_t(ImageSize*ImageSize/1000)&&mean<.15,"Instanced and individual draws produce equivalent pixels");
}

void Illuminate(Scene& scene){
    scene.settings.directionalLight=true;scene.settings.lightDirection={-.5f,-.8f,-1};scene.settings.lightIntensity=2;
}

GameObject& Cube(Scene& scene,Vector3 position,const Ref<Texture2D>& texture,Color color=Color(1)){
    auto& object=scene.CreateGameObject("Instance cube");object.transform.position=position;
    object.AddComponent<MeshRenderer>().color=color;
    if(texture)object.AddComponent<Material>().albedoTexture=texture;
    return object;
}

void ThousandCubes(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;Illuminate(scene);auto atlas=CreateRef<AtlasTexture>();
    for(int z=0;z<10;++z)for(int y=0;y<10;++y)for(int x=0;x<10;++x)
        Cube(scene,Vector3(x-4.5f,y-4.5f,z-4.5f)*1.2f,atlas,{.7f+.03f*x,.7f+.03f*y,.7f+.03f*z,1});
    PerspectiveCamera camera(45,1,.1f,100);
    for(Vector3 eye:{Vector3(16,13,24),Vector3(-19,9,23),Vector3(9,-12,25)}){
        camera.SetPosition(eye);camera.LookAtObj({0,0,0});
        auto before=Render(renderer,target,scene,camera,false);
        auto after=Render(renderer,target,scene,camera,true);
        Check(before.draws==1000&&before.batches==0&&before.instances==0,"Disabled instancing submits 1000 individual cubes");
        Check(after.draws==1&&after.batches==1&&after.instances==1000,"1000 compatible colored cubes use one instanced draw");
        Equivalent(before,after,"1000 cubes moving camera",3000);
    }
}

void MixedState(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;Illuminate(scene);auto atlas=CreateRef<AtlasTexture>(),other=CreateRef<AtlasTexture>(true);
    auto& parent=scene.CreateGameObject("Transformed parent");
    parent.transform.position={-.7f,.3f,0};parent.transform.rotation={0,25,8};parent.transform.localScale={-1.1f,1.2f,.7f};
    for(int i=0;i<4;++i){
        auto& object=Cube(scene,{float(i)-1.5f,.8f,0},atlas,{.3f+.2f*i,1.f-.15f*i,.5f,1});object.SetParent(&parent);
        object.transform.rotation={float(i)*9,float(i)*17,0};object.transform.localScale={.7f,.5f+.15f*i,.8f};
    }
    for(int i=0;i<2;++i){
        auto& object=Cube(scene,{float(i)*1.2f-2,-1.2f,0},atlas);
        auto& material=*object.GetComponent<Material>();material.metallicity=.8f;material.roughness=.2f;
    }
    for(int i=0;i<2;++i)Cube(scene,{float(i)*1.2f+.5f,-1.2f,0},other);
    auto& sphere=scene.CreateGameObject("Sphere fallback");sphere.transform.position={3,1,0};
    sphere.AddComponent<MeshRenderer>().shape=int(BasicShape::Sphere);
    auto& sprite=scene.CreateGameObject("Sprite between batches");sprite.transform.position={-3,-.4f,1};
    sprite.AddComponent<SpriteRenderer>().color={1,.2f,.1f,1};
    for(int i=0;i<2;++i)Cube(scene,{float(i)*1.2f+.7f,1.8f,0},atlas);
    auto& dual=Cube(scene,{-3,1.8f,0},atlas);dual.AddComponent<SpriteRenderer>().color={.1f,.2f,1,1};
    auto& custom=scene.CreateGameObject("Triangle material fallback");custom.transform.position={3,-1.5f,0};
    auto& mesh=custom.AddComponent<MeshRenderer>();mesh.SetShape(int(BasicShape::Custom));
    SurfaceMaterial surface;surface.albedoTexture=other;surface.metallicity=.7f;surface.roughness=.3f;
    mesh.triangleMaterials.faces[0]=surface;mesh.triangleMaterials.faces[1]=surface;
    auto& inactive=Cube(scene,{0,0,3},atlas,{1,0,0,1});inactive.SetActive(false);
    auto& disabled=Cube(scene,{0,0,3},atlas,{0,1,0,1});disabled.GetComponent<MeshRenderer>()->enabled=false;
    auto& zero=Cube(scene,{0,0,3},atlas);zero.transform.localScale={0,1,1};
    auto& excluded=Cube(scene,{0,0,3},atlas,{0,0,1,1});
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({1,2,12});camera.LookAtObj({0,0,0});
    auto before=Render(renderer,target,scene,camera,false,excluded.GetID());
    auto after=Render(renderer,target,scene,camera,true,excluded.GetID());
    Equivalent(before,after,"Mixed materials, transforms, atlas and fallbacks");
    Check(after.instances==10,"Only the 10 eligible active cubes enter instance buffers");
    Check(after.batches>=4&&after.draws<before.draws,"Material and texture changes split batches without losing draw reduction");
    // Toggle after an instanced draw and then move a parent: no stale divisor, transform or color state may leak.
    parent.transform.position.x+=.6f;parent.transform.rotation.z-=15;
    Equivalent(Render(renderer,target,scene,camera,false,excluded.GetID()),Render(renderer,target,scene,camera,true,excluded.GetID()),"Moved parent and renderer mode toggle");
}

void ReflectionFallback(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;Illuminate(scene);
    auto& mirror=scene.CreateGameObject("Reflective cube fallback");mirror.transform.localScale={3,3,.3f};
    mirror.AddComponent<MeshRenderer>().color=Color(1);auto& material=mirror.AddComponent<Material>();material.metallicity=1;material.roughness=.15f;
    auto& probe=mirror.AddComponent<ReflectionProbe>();probe.resolution=64;probe.maxObjectSize=0;
    Cube(scene,{-1.5f,.4f,2},{},{1,.1f,.1f,1});
    Cube(scene,{1.5f,-.3f,2},{},{.1f,1,.1f,1});
    PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({0,0,7});camera.LookAtObj({0,0,0});
    auto before=Render(renderer,target,scene,camera,false);
    auto after=Render(renderer,target,scene,camera,true);
    Equivalent(before,after,"Reflection fallback and recursive probe captures",2000);
    Check(before.draws==3,"Reflection capture draws do not pollute main-pass statistics");
    Check(after.draws==2&&after.batches==1&&after.instances==2,"Reflective cube falls back while compatible unmaterialed cubes instance");
}
}

int main(){
    GLFWwindow* window=nullptr;
    try{
        Debug::Init();Check(glfwInit()!=0,"GLFW initialization");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        window=glfwCreateWindow(64,64,"Instancing regression",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");
        glfwMakeContextCurrent(window);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
        {
            RenderCommand::SetDepthTestEnabled(true);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);
            SceneRenderer renderer;ViewportTarget target;
            ThousandCubes(renderer,target);MixedState(renderer,target);ReflectionFallback(renderer,target);
        }
        glfwDestroyWindow(window);glfwTerminate();
        std::cout<<"Cube instancing draw reduction and framebuffer regressions passed\n";return 0;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;
    }
}
