#include "Project/Workspace.h"
#include "Project/FbxModel.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace Rice {
namespace {
bool Inside(const fs::path& relative) {
    return !relative.empty() && !relative.has_root_path() && *relative.begin() != "..";
}

bool SameFileContents(const fs::path& a, const fs::path& b) {
    if (fs::equivalent(a, b)) return true;
    if (fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream first(a, std::ios::binary), second(b, std::ios::binary);
    if (!first || !second) throw std::runtime_error("Cannot compare imported assets");
    std::array<char, 32768> left, right;
    do {
        first.read(left.data(), left.size());
        second.read(right.data(), right.size());
        if (first.bad() || second.bad()) throw std::runtime_error("Cannot read imported assets");
        if (first.gcount() != second.gcount() || !std::equal(left.begin(), left.begin()+first.gcount(), right.begin()))
            return false;
    } while (first);
    return true;
}

// Keep the FBX and referenced images portable. Repeated imports reuse existing
// files. A different model or image with the same name gets a distinct path.
class ImportFiles {
    const fs::path root;
    std::vector<fs::path> created;
public:
    explicit ImportFiles(const fs::path& root) : root(fs::canonical(root)) {}

    fs::path Copy(const fs::path& source, const fs::path& relative) {
        const auto inProject = fs::canonical(source).lexically_relative(root);
        if (Inside(inProject)) return inProject;
        const auto destination = fs::weakly_canonical(root / relative);
        if (!Inside(destination.lexically_relative(root))) throw std::runtime_error("Invalid FBX asset path");
        fs::create_directories(destination.parent_path());
        for (size_t number=1;;++number) {
            auto candidate = destination;
            if (number>1) candidate.replace_filename(destination.stem().wstring()+L" ("+std::to_wstring(number)+L")"+destination.extension().wstring());
            auto meta = fs::path(candidate).concat(".meta");
            if (fs::is_regular_file(candidate) && SameFileContents(source,candidate)) return candidate.lexically_relative(root);
            if (fs::exists(candidate) || fs::exists(meta)) continue;
            if (!fs::copy_file(source,candidate,fs::copy_options::skip_existing)) continue;
            created.push_back(candidate);
            return candidate.lexically_relative(root);
        }
    }

    void Rollback() const {
        std::error_code ignored;
        for (const auto& path : created) {
            fs::remove(path, ignored);
            fs::remove(fs::path(path).concat(".meta"), ignored);
        }
    }
};

// FBX exporters commonly write both an authoring-machine absolute path and
// a relative texture name. Resolve sidecars under the selected FBX directory.
fs::path TextureName(const fs::path& reference) {
    auto name=reference.lexically_normal();
    if (!Inside(name)) name=reference.filename();
    for (const auto& part:name) {
        if (part.native().find_first_of(L"<>:\"|?*")!=std::wstring::npos) return {};
    }
    auto extension=ProjectFile::Utf8(name.extension());
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    static const std::set<std::string> supported{".png",".jpg",".jpeg",".tga",".bmp",".gif",".psd",".hdr",".pic",".ppm",".pgm"};
    return supported.contains(extension)?name:fs::path{};
}

fs::path TextureSource(const fs::path& directory, const fs::path& name) {
    for (const auto& candidate : {directory/name,directory/name.filename()}) {
        auto resolved=fs::weakly_canonical(candidate);
        if (Inside(resolved.lexically_relative(directory)) && fs::is_regular_file(resolved)) return resolved;
    }
    return {};
}

fs::path StoreEmbedded(ImportFiles& files,const fs::path& temp,const fs::path& name,const std::string& bytes) {
    auto staging=temp/("FBX-"+NewAssetID()+".tmp");
    struct RemoveTemp {
        fs::path path;
        ~RemoveTemp(){std::error_code ignored;fs::remove(path,ignored);}
    } cleanup{staging};
    std::ofstream out(staging,std::ios::binary);
    out.write(bytes.data(),bytes.size());
    out.close();
    if (!out) throw std::runtime_error("Cannot extract the embedded FBX texture");
    return files.Copy(staging,name);
}
}

EntityID Workspace::ImportFbx(const fs::path& source) {
    if (editor.playing) throw std::runtime_error("Stop Play before importing a model");
    const auto input=fs::canonical(source);
    auto parts=ReadFbx(input);
    if (parts.empty()) throw std::runtime_error("The FBX has no importable mesh");
    if (!SceneCodec::FitsObjectLimit(*editor.scene,parts.size()+1))
        throw std::runtime_error("Import would exceed the scene limit of 10,000 objects");
    ImportFiles files(project->assets.Root());
    auto& scene=*editor.scene;
    EntityID rootId=0;
    std::set<std::string> missing;
    size_t meshes=0;
    try {
        auto model=files.Copy(input,input.filename());
        std::map<std::pair<fs::path,const std::string*>,fs::path> textures;
        std::vector<fs::path> paths;
        for (const auto& part:parts) {
            const auto& reference=part.albedoTexture;
            auto [it,added]=textures.try_emplace({reference,part.embeddedAlbedo.get()});
            if (added && (!reference.empty() || part.embeddedAlbedo)) {
                auto name=TextureName(reference);
                if (!name.empty()) {
                    auto relative=model.parent_path()/name;
                    if (part.embeddedAlbedo)
                        it->second=StoreEmbedded(files,project->root/"Temp",relative,*part.embeddedAlbedo);
                    else if (auto external=TextureSource(input.parent_path(),name);!external.empty())
                        it->second=files.Copy(external,relative);
                }
                if (it->second.empty()) missing.insert(ProjectFile::Utf8(reference));
            }
            paths.push_back(it->second);
        }
        project->assets.Refresh();
        std::map<fs::path,std::string> ids;
        for (const auto& entry:project->assets.Entries()) ids.emplace(entry.path,entry.id);
        editor.Edit([&](Scene& current) {
            auto& root=current.CreateGameObject(ProjectFile::Utf8(input.stem()));
            rootId=root.GetID();
            SceneLayers::PlaceRoot(root,current.Find(editor.selected));
            std::vector<GameObject*> objects;
            objects.reserve(parts.size());
            for (size_t i=0;i<parts.size();++i) {
                auto& part=parts[i];
                auto& parent=part.parent<0?root:*objects.at(part.parent);
                auto& object=current.CreateGameObject(part.name);
                object.SetParent(&parent);
                objects.push_back(&object);
                object.transform.position=part.position;
                if (part.mesh.Empty()) continue;
                auto& renderer=object.AddComponent<MeshRenderer>();
                renderer.shape=int(BasicShape::Custom);
                renderer.custom=std::move(part.mesh);
                renderer.color=part.color;
                if (!paths[i].empty()) object.AddComponent<Material>().albedoAsset=ids.at(paths[i]);
                ++meshes;
            }
            if (SceneCodec(registry).Encode(current).size()>ProjectFile::MaxSceneBytes)
                throw std::runtime_error("Import would exceed the 16 MiB scene limit");
        });
    } catch (...) {
        if (rootId) {scene.Destroy(rootId);scene.FlushDestroyed();}
        files.Rollback();
        try {project->assets.Refresh();} catch (const std::exception& error) {Report(error);}
        throw;
    }
    editor.Select(rootId);
    Log("Imported "+ProjectFile::Utf8(input.filename())+" ("+std::to_string(meshes)+" meshes)");
    for (const auto& name:missing) Log("FBX base color texture unavailable: "+name,spdlog::level::warn);
    return rootId;
}
}
