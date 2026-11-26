#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Scene/Components.h"
#include "Renderer/ViewportTarget.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include "Project/SceneCodec.h"
#include <array>
#include <iostream>
using namespace Rice;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct ImageBounds {
    Vector2 low{std::numeric_limits<float>::max()};
    Vector2 high{-std::numeric_limits<float>::max()};
    size_t pixels=0;
    void Include(Vector2 point){low=glm::min(low,point);high=glm::max(high,point);++pixels;}
};
static ImageBounds ProjectMarker(const Camera& camera,Vector3 center,Vector2 size,int imageSize){
    ImageBounds result;
    for(int corner=0;corner<4;++corner){
        Vector3 point=center+Vector3((corner&1?.5f:-.5f)*size.x,(corner&2?.5f:-.5f)*size.y,0);
        auto projected=camera.GetVPMatrix()*Vector4(point,1);
        result.Include((Vector2(projected)/projected.w+Vector2(1))*(imageSize*.5f));
    }
    return result;
}
static void CheckReflectionImageGeometry(SceneRenderer& renderer,ViewportTarget& target){
    constexpr int imageSize=512;
    Scene scene;
    auto& mirror=scene.CreateGameObject("Geometry mirror");
    mirror.transform.localScale={4,4,1};
    mirror.AddComponent<MeshRenderer>().color=Color(1);
    auto& material=mirror.AddComponent<Material>();
    material.metallicity=1;material.roughness=0;
    auto& probe=mirror.AddComponent<ReflectionProbe>();
    probe.resolution=256;probe.maxObjectSize=0;
    auto& marker=scene.CreateGameObject("Reflection marker");
    marker.AddComponent<SpriteRenderer>().color={1,0,0,1};
    const Vector2 markerSize(.25f,.25f);
    marker.transform.localScale=Vector3(markerSize,1);
    PerspectiveCamera camera(45,1,.1f,100);
    camera.SetPosition({0,0,5});camera.LookAtObj({0,0,0});
    float maximumCenterError=0,maximumExtentError=0;
    int checkedImages=0;
    auto checkImage=[&]{
        std::vector<unsigned char> pixels(imageSize*imageSize*4);
        target.Render(imageSize,imageSize,[&]{
            renderer.Render(scene,camera);
            glReadPixels(0,0,imageSize,imageSize,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        });
        // Reflect all source corners across the actual front face, independently of probe sampling.
        auto world=mirror.GetWorldMatrix();
        auto planePoint=Vector3(world*Vector4(0,0,.5f,1));
        auto planeNormal=glm::normalize(glm::transpose(glm::inverse(glm::mat3(world)))*Vector3(0,0,1));
        ImageBounds expected;
        for(int corner=0;corner<4;++corner){
            auto point=marker.transform.position+Vector3((corner&1?.5f:-.5f)*markerSize.x,
                                                        (corner&2?.5f:-.5f)*markerSize.y,0);
            auto virtualPoint=point-2.f*planeNormal*glm::dot(point-planePoint,planeNormal);
            auto projected=camera.GetVPMatrix()*Vector4(virtualPoint,1);
            expected.Include((Vector2(projected)/projected.w+Vector2(1))*(imageSize*.5f));
        }
        auto direct=ProjectMarker(camera,marker.transform.position,markerSize,imageSize);
        ImageBounds actual;
        for(int y=0;y<imageSize;++y)for(int x=0;x<imageSize;++x){
            Vector2 pixelCenter(x+.5f,y+.5f);
            bool directSource=pixelCenter.x>=direct.low.x-1 && pixelCenter.x<=direct.high.x+1 &&
                              pixelCenter.y>=direct.low.y-1 && pixelCenter.y<=direct.high.y+1;
            auto offset=(y*imageSize+x)*4;
            if(!directSource && pixels[offset]>100 && pixels[offset]>pixels[offset+1]+80 &&
               pixels[offset]>pixels[offset+2]+80)actual.Include(pixelCenter);
        }
        Check(actual.pixels>20,"Reflected marker retains a visible spatial image");
        auto centerError=glm::abs((actual.low+actual.high-expected.low-expected.high)*.5f);
        auto extentError=glm::abs((actual.high-actual.low)-(expected.high-expected.low));
        maximumCenterError=std::max({maximumCenterError,centerError.x,centerError.y});
        maximumExtentError=std::max({maximumExtentError,extentError.x,extentError.y});
        ++checkedImages;
        Check(centerError.x<=3 && centerError.y<=3,"Reflected image position matches analytic mirrored source projection");
        Check(extentError.x<=6 && extentError.y<=6,"Reflected image dimensions match analytic mirrored source projection");
    };
    for(Vector3 markerPosition:{Vector3(.5f,0,3),Vector3(.7f,.15f,4)}){
        marker.transform.position=markerPosition;
        probe.maxObjectSize=0;checkImage();
        probe.maxObjectSize=5000;checkImage();
    }
    camera.SetPosition({-.3f,.2f,5});camera.LookAtObj({-.3f,.2f,0});
    probe.maxObjectSize=0;checkImage();
    probe.maxObjectSize=5000;checkImage();
    probe.enabled=false;
    auto& separateProbe=scene.CreateGameObject("Offset reflection probe");
    separateProbe.transform.position={.3f,.2f,1};
    auto& offsetProbe=separateProbe.AddComponent<ReflectionProbe>();
    offsetProbe.resolution=256;
    offsetProbe.maxObjectSize=0;checkImage();
    offsetProbe.maxObjectSize=5000;checkImage();
    // The analytical reflection also accounts for a rotated receiver plane.
    mirror.transform.rotation.y=15;
    camera.SetPosition({0,0,5});camera.LookAtObj({0,0,0});
    marker.transform.position={.5f,0,3};
    offsetProbe.maxObjectSize=0;checkImage();
    offsetProbe.maxObjectSize=5000;checkImage();
    std::cout<<"Reflection image geometry checked "<<checkedImages<<" images; maximum center error "
             <<maximumCenterError<<" px, maximum extent error "<<maximumExtentError<<" px\n";
}
class TestSkyTexture : public Texture2D {
    GLuint texture=0;
public:
    TestSkyTexture(){glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);unsigned char pixel[]={30,80,160,255};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
    ~TestSkyTexture(){glDeleteTextures(1,&texture);}
    uint32_t GetWidth()const override{return 1;}uint32_t GetHeight()const override{return 1;}
    void Bind(uint32_t slot)const override{glActiveTexture(GL_TEXTURE0+slot);glBindTexture(GL_TEXTURE_2D,texture);}
};
static void CheckReflectionModes(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;
    scene.settings.skybox=true;
    scene.skyboxTexture=CreateRef<TestSkyTexture>();
    auto& probeObject=scene.CreateGameObject("Shared probe");
    probeObject.transform.position={0,0,1};
    auto& probe=probeObject.AddComponent<ReflectionProbe>();
    probe.resolution=256;
    probe.maxObjectSize=5000;
    auto receiver=[&](const char* name,float x)->GameObject&{
        auto& object=scene.CreateGameObject(name);
        object.transform.position.x=x;
        auto& mesh=object.AddComponent<MeshRenderer>();
        mesh.shape=int(BasicShape::Rectangle);
        mesh.color=Color(1);
        mesh.reflectionMode=int(ReflectionMode::Direct);
        auto& material=object.AddComponent<Material>();
        material.metallicity=1;material.roughness=0;
        return object;
    };
    auto& left=receiver("Left mirror",-.8f);
    auto& right=receiver("Right mirror",.8f);
    auto& marker=scene.CreateGameObject("Red marker");
    auto& source=marker.AddComponent<MeshRenderer>();
    source.shape=int(BasicShape::Custom);
    // A planar 2,048-triangle mesh with a local origin outside its bounds.
    // Different points on the mirror send rays into different BVH leaves.
    constexpr uint32_t cells=32,stride=cells+1;
    for(uint32_t y=0;y<=cells;++y)for(uint32_t x=0;x<=cells;++x)
        source.custom.positions.emplace_back(2.5f+float(x)/cells,-.5f+float(y)/cells,0);
    for(uint32_t y=0;y<cells;++y)for(uint32_t x=0;x<cells;++x){
        uint32_t a=y*stride+x,b=a+1,c=a+stride,d=c+1;
        source.custom.indices.insert(source.custom.indices.end(),{a,b,d,a,d,c});
    }
    source.custom.uv.resize(source.custom.indices.size(),Vector2(0));
    source.custom.Touch();
    source.color={1,0,0,1};
    source.reflectionMode=int(ReflectionMode::Off);
    marker.AddComponent<Material>();
    marker.transform.localScale={.85f,1.2f,1};
    marker.transform.rotation.z=20;
    marker.TranslateWorld(Vector3(1.92f,0,7)-marker.GetWorldPivot());

    // At X=0.8 the reflected ray reaches X=1.92 on the marker plane. A parallel
    // ray sampled from the probe reaches X=0.96 and sees the blue sky instead.
    constexpr int size=128;
    PerspectiveCamera camera(45,1,.1f,100);
    camera.SetPosition({0,0,5});camera.LookAtObj({0,0,0});
    using Pixel=std::array<unsigned char,4>;
    auto sample=[&]{
        std::array<Vector3,4> points{left.GetWorldPivot(),right.GetWorldPivot(),
            right.GetWorldPivot()+Vector3(.1f,.08f,0),right.GetWorldPivot()-Vector3(.1f,.08f,0)};
        std::array<Pixel,4> pixels{};
        target.Render(size,size,[&]{
            renderer.Render(scene,camera);
            for(size_t i=0;i<pixels.size();++i){
                auto clip=camera.GetVPMatrix()*Vector4(points[i],1);
                auto pixel=(Vector2(clip)/clip.w+1.f)*(.5f*size);
                glReadPixels(int(pixel.x),int(pixel.y),1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[i].data());
            }
        });
        return pixels;
    };
    auto direct=sample();
    Check(direct[0][2]>direct[0][0]+80&&direct[1][2]>direct[1][0]+80,"Direct receivers sample the sky from the shared cubemap");
    Check(renderer.GetSpatialStats().reflectionGeometryUploads==0,"A direct-only probe skips the intersection geometry upload");
    right.GetComponent<MeshRenderer>()->reflectionMode=int(ReflectionMode::Precise);
    auto mixed=sample();
    Check(renderer.GetSpatialStats().reflectionGeometryUploads==1,"A cached direct probe uploads geometry for its first precise receiver");
    Check(mixed[0]==direct[0]&&std::all_of(mixed.begin()+1,mixed.end(),[](const Pixel& pixel){return pixel[0]>pixel[2]+20;}),
          "Precise rays hit different parts of the transformed grid while a direct receiver still samples the sky");
    right.GetComponent<MeshRenderer>()->reflectionMode=int(ReflectionMode::Off);
    auto off=sample();
    Check(off[0]==direct[0]&&mixed[1][0]>off[1][0]+20&&direct[1][2]>off[1][2]+80,
          "Off disables receiving; an Off marker is still captured and visible in the precise reflection");

    ComponentRegistry registry;SceneCodec codec(registry);
    auto encoded=codec.Encode(scene);
    auto restored=codec.Decode(encoded);
    Check(restored->Find(left.GetID())->GetComponent<MeshRenderer>()->reflectionMode==int(ReflectionMode::Direct)&&
          restored->Find(right.GetID())->GetComponent<MeshRenderer>()->reflectionMode==int(ReflectionMode::Off),
          "Reflection modes survive scene roundtrip");
    std::istringstream lines(encoded);std::ostringstream legacy;std::string line;
    while(std::getline(lines,line))if(!line.starts_with("\"Reflection mode\""))legacy<<line<<'\n';
    auto old=codec.Decode(legacy.str());
    Check(old->Find(left.GetID())->GetComponent<MeshRenderer>()->reflectionMode==int(ReflectionMode::Precise),
          "A scene with no reflection mode retains precise reflections");
    auto snapshot=ComponentSnapshot::Capture(*registry.Find("MeshRenderer"),*left.GetComponent<MeshRenderer>());
    auto& copy=scene.CreateGameObject("Snapshot copy");
    ComponentSnapshot::Apply(copy,{snapshot},registry);
    Check(copy.GetComponent<MeshRenderer>()->reflectionMode==int(ReflectionMode::Direct),"Mesh snapshots preserve the receiver mode");
    snapshot.data.erase(snapshot.data.rfind('\n',snapshot.data.size()-2)+1);
    ComponentSnapshot::Apply(copy,{snapshot},registry);
    Check(copy.GetComponent<MeshRenderer>()->reflectionMode==int(ReflectionMode::Precise),
          "A legacy snapshot restores the default mode on an existing mesh");
}
static void CheckEnvironment(SceneRenderer& renderer,ViewportTarget& target){
    Scene scene;PerspectiveCamera camera(50,1,.1f,1000);camera.SetPosition({4,3,5});camera.LookAtObj({0,0,0});
    auto image=[&](bool guides=false){std::vector<unsigned char> pixels(96*96*4);target.Render(96,96,[&]{renderer.Render(scene,camera,false,0,guides);glReadPixels(0,0,96,96,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());});return pixels;};
    auto background=image();scene.settings.ground=true;auto ground=image();
    auto center=(48*96+48)*4;Check(ground[center]>170&&ground[center+1]==ground[center],"Ground is light gray at y=0");
    camera.SetPosition({100004,3,100005});camera.LookAtObj({100000,0,100000});auto distant=image();
    Check(distant[center]>170,"Ground continues at distant world coordinates");
    scene.settings.ground=false;camera.SetPosition({4,3,5});camera.LookAtObj({0,0,0});
    scene.settings.axes=scene.settings.grids=Vector3(0);
    Check(image(true)==background,"Disabled editor guides draw nothing");
    for(int i=0;i<3;++i){scene.settings.axes=Vector3(0);scene.settings.axes[i]=1;
        Check(image(true)!=background,"Every editor axis renders independently");Check(image()==background,"Axes never render in runtime view");}
    scene.settings.axes=Vector3(0);
    for(int i=0;i<3;++i){scene.settings.grids=Vector3(0);scene.settings.grids[i]=1;
        Check(image(true)!=background,"Every editor grid plane renders independently");Check(image()==background,"Grids never render in runtime view");}
    scene.settings.skybox=true;scene.skyboxTexture=CreateRef<TestSkyTexture>();auto sky=image();
    Check(sky[center]==30&&sky[center+1]==80&&sky[center+2]==160,"Selected skybox texture fills background");
    scene.settings.skybox=false;Check(image()==background,"Skybox toggle restores background");
    auto& object=scene.CreateGameObject("Lit cube");object.AddComponent<MeshRenderer>().color=Color(1);
    camera.SetPosition({0,0,5});camera.LookAtObj({0,0,0});auto dark=image();
    scene.settings.directionalLight=true;scene.settings.lightDirection={0,0,-1};scene.settings.lightColor={1,0,0,1};auto lit=image();
    Check(lit[center]>dark[center]+100&&lit[center+1]==dark[center+1],"Default directional light uses configured color and direction");
    scene.settings.lightIntensity=0;Check(image()==dark,"Default directional intensity is applied");
    Check(glGetError()==GL_NO_ERROR,"Environment rendering leaves no OpenGL errors");
}
int main(int argc,char** argv){GLFWwindow* window=nullptr;try{
    Debug::Init();Check(glfwInit()!=0,"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(64,64,"Light regression",nullptr,nullptr);Check(window!=nullptr,"Hidden GL context");glfwMakeContextCurrent(window);Check(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)!=0,"GL loader");
    {
        Scene scene;auto& mesh=scene.CreateGameObject();mesh.AddComponent<MeshRenderer>().color=Color(1);
        auto& parent=scene.CreateGameObject();auto& lamp=scene.CreateGameObject();lamp.SetParent(&parent);auto& light=lamp.AddComponent<Light>();lamp.transform.position={0,0,2};light.range=10;
        PerspectiveCamera camera(45,1,.1f,100);camera.SetPosition({0,0,5});camera.LookAtObj({0,0,0});SceneRenderer renderer;ViewportTarget target;RenderCommand::SetDepthTestEnabled(true);
        auto sample=[&]{unsigned char pixel[4]{};target.Render(64,64,[&]{renderer.Render(scene,camera);glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);});return int(pixel[0]);};
        int near=sample();lamp.transform.position.z=-2;int behind=sample();Check(near>behind+50,"Point translation updates actual shader lighting without gameplay Update");
        lamp.transform.position.z=2;parent.transform.position.x=20;Check(sample()<near-50,"Parent translation updates point light world position");parent.transform.position.x=0;
        auto& group=scene.CreateGameObject();group.isGroup=true;lamp.SetGroup(&group);group.transform.position.y=20;Check(sample()<near-50,"Group translation updates point lighting");lamp.SetGroup(nullptr);lamp.transform.position={0,0,2};
        light.type=1;light.direction={0,0,-1};int directional=sample();parent.transform.rotation.y=180;Check(sample()<directional-50,"Directional light uses parent world rotation");parent.transform.rotation.y=0;
        light.direction={-1,-1,-1};int beforeScale=sample();auto directionBefore=light.WorldDirection();
        lamp.transform.localScale={8,.2f,3};parent.transform.localScale={.3f,5,2};
        Check(glm::length(light.WorldDirection()-directionBefore)<.0001f&&sample()==beforeScale,"Local and parent nonuniform scale cannot steer directional light");
        lamp.transform.localScale={-3,0,8};Check(glm::length(light.WorldDirection()-directionBefore)<.0001f&&sample()==beforeScale,"Zero and negative scale do not steer directional light");
        lamp.transform.localScale={1,1,1};parent.transform.localScale={1,1,1};
        lamp.transform.rotation.y=90;Check(glm::length(light.WorldDirection()-directionBefore)>.5f&&sample()!=beforeScale,"Local rotation still steers light");lamp.transform.rotation.y=0;
        group.transform=Transform{};lamp.SetGroup(&group);auto groupedDirection=light.WorldDirection();int groupedPixel=sample();
        group.transform.localScale={6,.25f,2};Check(glm::length(light.WorldDirection()-groupedDirection)<.0001f&&sample()==groupedPixel,"Group scale cannot steer directional light");
        group.transform.rotation.y=90;Check(glm::length(light.WorldDirection()-groupedDirection)>.5f,"Group rotation steers directional light");
        group.transform=Transform{};lamp.SetGroup(nullptr);lamp.transform=Transform{};lamp.transform.position={0,0,2};
        light.type=0;light.range=.5f;Check(sample()<near-50,"Point range affects falloff");light.range=10;light.enabled=false;Check(sample()<near-50,"Disabled light removed immediately");
        ComponentRegistry registry;SceneCodec codec(registry);light.enabled=true;auto restored=codec.Decode(codec.Encode(scene));Check(restored->Find(lamp.GetID())->GetComponent<Light>()->range==10,"New light properties roundtrip");
        auto& primitive=*mesh.GetComponent<MeshRenderer>();
        for(int shape=0;shape<6;++shape) {
            primitive.shape=shape;
            Check(sample()>0,"Each basic shape produces visible geometry");
            auto shapeScene=codec.Decode(codec.Encode(scene));
            Check(shapeScene->Find(mesh.GetID())->GetComponent<MeshRenderer>()->shape==shape,"Shape survives serialization");
        }
        primitive.shape=0;
        auto& managerObject=scene.CreateGameObject("Manager");
        auto& manager=managerObject.AddComponent<SingletonManager>();
        Check(SingletonManager::Instance(scene)==&manager,"Manager lookup is scene scoped");
        manager.enabled=false;
        Check(SingletonManager::Instance(scene)==nullptr,"Disabled managers do not own the singleton");
        auto& blank=scene.CreateGameObject("Blank");
        blank.AddComponent<BlankScript>();
        blank.AddComponent<BoxCollider>();
        restored=codec.Decode(codec.Encode(scene));
        Check(restored->Find(blank.GetID())->GetComponent<BlankScript>()!=nullptr,"Blank script survives serialization");
        light.type=1;
        light.direction={0,0,-1};
        light.intensity=.25f;
        int legacyPixel=sample();
        auto& material=mesh.AddComponent<Material>();
        material.roughness=.5f;
        int dielectric=sample();
        material.metallicity=1.f;
        int polished=sample();
        material.roughness=1.f;
        Check(polished>dielectric+10 && polished>sample()+10,"Metallicity and roughness change the rendered highlight");
        material.enabled=false;
        Check(sample()==legacyPixel,"Disabling Material restores the previous mesh shading");
        material.albedoAsset="test-albedo";
        restored=codec.Decode(codec.Encode(scene));
        auto* savedMaterial=restored->Find(mesh.GetID())->GetComponent<Material>();
        Check(savedMaterial && !savedMaterial->enabled && savedMaterial->metallicity==1.f &&
              savedMaterial->roughness==1.f && savedMaterial->albedoAsset=="test-albedo","Material properties survive scene roundtrip");
        light.enabled=false;
        material.enabled=true;
        material.albedoAsset.clear();
        primitive.shape=0;
        const Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        const Vector2 grids[]={{3,4},{4,3},{6,1},{1,6}};
        const Vector2 vertical[]={{1,1},{1,3},{1,0},{1,2},{0,1},{2,1}};
        const Vector2 horizontal[]={{2,1},{0,1},{1,0},{1,2},{1,1},{3,1}};
        for(int layout=1;layout<=4;++layout){
            int width=int(grids[layout-1].x)*8,height=int(grids[layout-1].y)*8;
            std::vector<unsigned char> pixels(width*height*3,0);
            for(int face=0;face<6;++face){
                auto cell=layout==1?vertical[face]:layout==2?horizontal[face]:layout==3?Vector2(face,0):Vector2(0,face);
                for(int y=0;y<8;++y)for(int x=0;x<8;++x){
                    auto offset=((int(cell.y)*8+y)*width+int(cell.x)*8+x)*3;
                    pixels[offset+2]=static_cast<unsigned char>(40+face*35);
                }
            }
            auto path=fs::temp_directory_path()/("rice-cubemap-"+NewAssetID()+".tga");
            unsigned char header[18]{};
            header[2]=2;header[12]=static_cast<unsigned char>(width);header[14]=static_cast<unsigned char>(height);header[16]=24;header[17]=32;
            {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(header),18);file.write(reinterpret_cast<char*>(pixels.data()),pixels.size());}
            material.albedoTexture=Texture2D::Create(path.string());
            fs::remove(path);
            Check(int(DetectCubemapLayout(width,height))==layout,"Each supported cubemap layout is recognized");
            for(int face=0;face<6;++face){
                camera.SetPosition(directions[face]*5.f);
                // Avoid the look-at up vector singularity on the Y axis.
                camera.LookAtObj(face==2||face==3?Vector3(.001f,0,0):Vector3(0));
                Check(std::abs(sample()-int((40+face*35)*.15f))<=2,"Cubemap face labels map to the correct world axes");
            }
        }
        material.albedoTexture=Texture2D::Create((fs::path(__FILE__).parent_path().parent_path()/"Sandbox/Assets/grass.png").string());
        Check(material.albedoTexture && DetectCubemapLayout(material.albedoTexture->GetWidth(),material.albedoTexture->GetHeight())==CubemapLayout::HorizontalCross,"grass.png is a horizontal cross");
        camera.SetPosition({2,1.5f,3});camera.LookAtObj({0,0,0});
        light.enabled=true;light.type=1;light.direction={-1,-2,-3};light.intensity=2;
        material.metallicity=0;material.roughness=.7f;
        if(argc>1){
            std::vector<unsigned char> pixels(512*512*3);
            PerspectiveCamera preview(45,1,.1f,100);preview.SetPosition({2,1.5f,3});preview.LookAtObj({0,0,0});
            target.Render(512,512,[&]{renderer.Render(scene,preview);glReadPixels(0,0,512,512,GL_BGR,GL_UNSIGNED_BYTE,pixels.data());});
            unsigned char header[18]{};header[2]=2;header[13]=2;header[15]=2;header[16]=24;
            std::ofstream file(argv[1],std::ios::binary);file.write(reinterpret_cast<char*>(header),18);file.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
        }
        auto legacy=codec.Decode("RICE_SCENE 1\nObject 900 0 \"Old light\" 1 0 0 2 0 0 0 1 1 1\nComponent \"Light\"\n\"Enabled\" 1\n\"Color\" 1 1 1 1\n\"Intensity\" 2\n\"Direction\" -1 -1 -1\nEndComponent\nEndObject\nEndScene\n");
        auto* old=legacy->Find(900)->GetComponent<Light>();Check(old->type==0&&old->range==10&&old->intensity==2,"Legacy lights get point defaults and preserve properties");
        {
            Scene reflectedScene;
            auto& unrelated=reflectedScene.CreateGameObject("Other probe");
            unrelated.transform.position={50,0,0};
            auto& unrelatedProbe=unrelated.AddComponent<ReflectionProbe>();
            unrelatedProbe.resolution=16;
            unrelatedProbe.maxObjectSize=0;

            auto& mirror=reflectedScene.CreateGameObject("Mirror");
            mirror.AddComponent<MeshRenderer>().color=Color(1);
            auto& mirrorMaterial=mirror.AddComponent<Material>();
            mirrorMaterial.metallicity=1;mirrorMaterial.roughness=.05f;
            auto& probe=mirror.AddComponent<ReflectionProbe>();
            probe.resolution=32;probe.maxObjectSize=0;
            auto& source=reflectedScene.CreateGameObject("Behind camera");
            auto& sourceMesh=source.AddComponent<MeshRenderer>();
            sourceMesh.color={1,0,0,1};
            source.transform.position={0,0,7};source.transform.localScale={4,4,1};
            PerspectiveCamera mirrorCamera(45,1,.1f,100);mirrorCamera.SetPosition({0,0,5});mirrorCamera.LookAtObj({0,0,0});
            auto reflectedPixel=[&]{
                unsigned char pixel[4]{};
                target.Render(64,64,[&]{renderer.Render(reflectedScene,mirrorCamera);glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);});
                return int(pixel[0]);
            };
            probe.enabled=false;int noReflection=reflectedPixel();
            probe.enabled=true;int redReflection=reflectedPixel();
            Check(redReflection>noReflection+20,"Dynamic reflection sees objects behind the main camera");
            auto& sourceMaterial=source.AddComponent<Material>();
            sourceMaterial.metallicity=1;sourceMaterial.roughness=.5f;
            auto& captureLight=reflectedScene.CreateGameObject("Capture light").AddComponent<Light>();
            captureLight.type=1;captureLight.direction={0,0,1};captureLight.intensity=.5f;
            int metallicSource=reflectedPixel();
            sourceMaterial.metallicity=0;
            Check(std::abs(reflectedPixel()-metallicSource)>5,"Source Material metallicity affects captured reflections with multiple probes");
            captureLight.enabled=false;
            sourceMesh.color={0,1,0,1};
            Check(reflectedPixel()<redReflection-20,"Dynamic cubemap updates when scene color changes");
            sourceMesh.color={1,0,0,1};
            float projected=ReflectionPixelSize(mirror.GetWorldMatrix(),mirrorCamera.GetVPMatrix(),{64,64});
            probe.maxObjectSize=projected;
            Check(reflectedPixel()>noReflection+20,"Pixel size boundary includes receivers");
            probe.maxObjectSize=projected-.1f;
            Check(reflectedPixel()>noReflection+20,"Objects above threshold retain full dynamic reflection");
            Check(ReflectionPixelSize(mirror.GetWorldMatrix(),mirrorCamera.GetVPMatrix(),{128,128})>projected*1.99f,"Doubling viewport doubles projected pixel size");
            mirrorCamera.SetPosition({0,0,10});mirrorCamera.LookAtObj({0,0,0});
            Check(ReflectionPixelSize(mirror.GetWorldMatrix(),mirrorCamera.GetVPMatrix(),{64,64})<projected*.6f,"Camera distance changes reflection pixel threshold");
            mirrorCamera.SetPosition({0,0,5});mirrorCamera.LookAtObj({0,0,0});
            probe.maxObjectSize=.5f;
            Check(reflectedPixel()>noReflection+20,"Large objects retain full dynamic reflection");
            probe.maxObjectSize=100;
            int flatPixel=reflectedPixel();
            Check(flatPixel>noReflection+20,"Small objects keep a flat reflection");
            sourceMesh.color={0,1,0,1};
            Check(reflectedPixel()==flatPixel,"Small-only probes reuse their previous capture");
            probe.maxObjectSize=0;
            Check(reflectedPixel()<flatPixel-20,"Crossing the threshold resumes dynamic capture");
            sourceMesh.color={1,0,0,1};
            Check(reflectedPixel()>noReflection+20,"Dynamic capture resumes with current scene contents");
            probe.maxObjectSize=64;probe.storageQuality=1;probe.resolution=64;probe.fixEdgeSeams=false;
            bool previousSeamless=glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            Check(reflectedPixel()>noReflection+20,"HDR cubemap resize and seamless off retain reflections");
            Check(bool(glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS))==previousSeamless,"Reflection restores global seamless state");
            auto saved=codec.Decode(codec.Encode(reflectedScene));
            auto* savedProbe=saved->Find(mirror.GetID())->GetComponent<ReflectionProbe>();
            Check(savedProbe && savedProbe->resolution==64 && savedProbe->storageQuality==1 &&
                  savedProbe->maxObjectSize==64 && !savedProbe->fixEdgeSeams,"Reflection settings survive scene roundtrip");
        }
        CheckEnvironment(renderer,target);
        CheckReflectionModes(renderer,target);
        CheckReflectionImageGeometry(renderer,target);
        Check(glGetError()==GL_NO_ERROR,"Reflection geometry rendering completes without GL errors");
        std::cout<<"Lighting render regression passed; near="<<near<<", behind="<<behind<<'\n';
    }
    glfwDestroyWindow(window);glfwTerminate();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}}
