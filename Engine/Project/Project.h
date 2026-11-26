#pragma once
#include "GameBundle.h"
#include "Installation.h"
#include "ProjectFile.h"
#include "SceneCodec.h"
#include "SceneLayers.h"
#include "Scripting/NativeScriptCompiler.h"

namespace Rice {

std::string InstallBuiltinSkyboxes(AssetDatabase& assets);

class Project {
    struct Opened {
        fs::path root, file;
        ProjectFile data;
        std::vector<fs::path> legacyFiles;
        bool saved = false;
    };

    static fs::path DefaultFile(const fs::path& directory) {
        auto name = directory.filename();
        name += ".rice";
        return directory / name;
    }

    static fs::path Discover(const fs::path& directory) {
        if (!fs::is_directory(directory)) return {};
        auto named = DefaultFile(directory);
        if (fs::is_regular_file(named)) return named;
        if (fs::is_regular_file(directory / "project.rice")) return directory / "project.rice";
        fs::path candidate;
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".rice") continue;
            std::ifstream stream(entry.path());
            std::string magic;
            if (stream >> magic && magic == "RICE_PROJECT") {
                if (!candidate.empty()) throw std::runtime_error("Choose a specific Rice project file");
                candidate = entry.path();
            }
        }
        if (candidate.empty() && fs::is_regular_file(directory / "main.rice")) return directory / "main.rice";
        return candidate;
    }

    static Opened Inspect(const fs::path& selection, bool requireSaved) {
        if (selection.empty()) throw std::invalid_argument("Choose a project file or directory");
        auto selected = fs::absolute(selection).lexically_normal();
        if (selected.filename().empty()) selected = selected.parent_path();
        bool fileSelected = fs::is_regular_file(selected) ||
                            (!fs::exists(selected) && selected.extension() == ".rice");
        Opened result;
        result.root = fileSelected ? selected.parent_path() : selected;
        auto file = fileSelected ? selected : Discover(result.root);
        result.file = fileSelected ? selected : DefaultFile(result.root);
        if (file.empty() || !fs::is_regular_file(file)) {
            if (requireSaved) throw std::runtime_error("The selected path is not a saved Rice project");
            return result;
        }
        if (file.extension() != ".rice") throw std::invalid_argument("Choose a .rice project file");
        std::ifstream in(file, std::ios::binary);
        std::string magic;
        in >> magic;
        int version = 0;
        if (magic == "RICE_SCENE" && file.filename() == "main.rice") {
            auto manifest = result.root / "project.rice";
            if (fs::is_regular_file(manifest) && manifest != file) return Inspect(manifest, true);
            result.data.bindings = InputActions{}.keys;
        } else {
            in.clear();
            in.seekg(0);
            result.data = ProjectFile::Read(in, version);
        }
        result.saved = true;
        if (version == 3) {
            result.file = file;
            return result;
        }

        result.file = DefaultFile(result.root);
        if (magic == "RICE_PROJECT") result.legacyFiles.push_back(file);
        const auto canonicalRoot = fs::weakly_canonical(result.root);
        auto loadLegacy = [&](const fs::path& path) {
            auto relative = fs::weakly_canonical(path).lexically_relative(canonicalRoot);
            auto key = ProjectFile::SceneKey(ProjectFile::Utf8(path.lexically_relative(result.root)));
            if (relative.empty() || *relative.begin() == "..")
                throw std::runtime_error("Scene path escapes the project");
            if (fs::file_size(path) > 16 * 1024 * 1024) throw std::runtime_error("Scene exceeds 16 MiB");
            result.data.scenes.emplace(key, ReadFile(path));
            result.legacyFiles.push_back(path);
        };
        auto main = result.root / "main.rice";
        if (fs::is_regular_file(main)) loadLegacy(main);
        auto sceneDir = result.root / "Scenes";
        if (fs::is_directory(sceneDir)) for (const auto& entry : fs::directory_iterator(sceneDir))
            if (entry.is_regular_file() && entry.path().extension() == ".rice") loadLegacy(entry.path());
        if (!result.data.scenes.contains(result.data.activeScene))
            throw std::runtime_error("The active project scene is missing");
        return result;
    }

    fs::path file;
    ProjectFile data;
    std::vector<fs::path> legacyFiles;
    std::string defaultSkybox;

    explicit Project(Opened opened)
        : file(std::move(opened.file)), data(std::move(opened.data)),
          legacyFiles(std::move(opened.legacyFiles)), root(std::move(opened.root)), assets(root / "Assets") {
        defaultSkybox = InstallBuiltinSkyboxes(assets);
        if (opened.saved) input.keys = data.bindings;
        else {
            data.bindings = input.keys;
            CreateDirectories();
        }
    }

    void CreateDirectories() const {
        for (const auto* name : {"Assets", "Scripts", "Prefabs", "Settings", "Temp"})
            fs::create_directories(root / name);
    }

    void MoveLegacyDirectory(const fs::path& old, const fs::path& destination) {
        if (!fs::is_directory(old)) return;
        for (const auto& path : {old, destination}) {
            auto inside = fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(root));
            if (inside.empty() || *inside.begin() == "..") throw std::runtime_error("Legacy directory is outside the project");
        }
        if (fs::exists(destination) && !fs::is_directory(destination)) return;
        if (fs::is_directory(destination) && fs::is_empty(destination)) fs::remove(destination);
        if (!fs::exists(destination)) {
            fs::create_directories(destination.parent_path());
            fs::rename(old, destination);
            return;
        }
        // Older and current editor versions may both have saved content.
        // Merge disjoint entries and leave conflicting originals recoverable.
        for (const auto& entry : fs::directory_iterator(old)) {
            auto target = destination / entry.path().filename();
            if (entry.is_directory() && !entry.is_symlink()) MoveLegacyDirectory(entry.path(), target);
            else if (!fs::exists(target)) fs::rename(entry.path(), target);
        }
        if (fs::is_empty(old)) fs::remove(old);
    }

    static void Write(const fs::path& path, const ProjectFile& document) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        document.Write(out);
        out.close();
        if (!out) throw std::runtime_error("Cannot write project file");
    }

    // Publish the complete document in one rename. Loose files from older
    // projects are retained under Temp/Legacy for recovery.
    void Commit(ProjectFile next) {
        CreateDirectories();
        if (fs::is_directory(root / "Assets" / "Prefabs")) {
            MoveLegacyDirectory(root / "Assets" / "Prefabs", root / "Prefabs");
            assets.Refresh();
        }
        MoveLegacyDirectory(root / "Streaming", root / "Settings" / "Streaming");
        MoveLegacyDirectory(root / "ScriptModules", root / "Settings" / "RiceMonkey" / "Modules");
        MoveLegacyDirectory(root / "ScriptData", root / "Settings" / "RiceMonkey" / "Data");
        auto temp = root / "Temp" / (NewAssetID() + ".tmp");
        try {
            Write(temp, next);
            for (const auto& old : legacyFiles) {
                if (!fs::exists(old)) continue;
                auto backup = root / "Temp" / "Legacy" / old.lexically_relative(root);
                backup += ".legacy";
                fs::create_directories(backup.parent_path());
                fs::copy_file(old, backup, fs::copy_options::overwrite_existing);
            }
            fs::rename(temp, file);
        } catch (...) {
            std::error_code ignored;
            fs::remove(temp, ignored);
            throw;
        }
        data = std::move(next);
        for (const auto& old : legacyFiles)
            if (old != file) fs::remove(old);
        auto looseScenes = root / "Scenes";
        if (!legacyFiles.empty() && fs::is_directory(looseScenes) && fs::is_empty(looseScenes))
            fs::remove(looseScenes);
        legacyFiles.clear();
    }

    void ValidateAssets(Scene& scene) const {
        for (auto& object : scene.GetObjects()) {
            if (auto* material = object->GetComponent<Material>(); material && !material->albedoAsset.empty())
                assets.Resolve(material->albedoAsset);
            if (auto* mesh = object->GetComponent<MeshRenderer>())
                for (const auto& [index, value] : mesh->triangleMaterials.faces)
                    if (!value.albedoAsset.empty()) assets.Resolve(value.albedoAsset);
            if (auto* sprite = object->GetComponent<SpriteRenderer>(); sprite && !sprite->asset.empty())
                assets.Resolve(sprite->asset);
            if (auto* audio = object->GetComponent<AudioSource>(); audio && !audio->clip.empty())
                assets.Resolve(audio->clip);
        }
        if (scene.settings.skybox && !scene.settings.skyboxAsset.empty()) assets.Resolve(scene.settings.skyboxAsset);
    }

    // Project data only. Temp, script sources, publication history and editor
    // backups are not runtime inputs.
    static void CopyContent(const fs::path& source, const fs::path& destination, bool settings = false) {
        fs::create_directories(destination);
        if (!fs::is_directory(source)) return;
        for (auto it = fs::recursive_directory_iterator(source); it != fs::recursive_directory_iterator(); ++it) {
            auto relative = it->path().lexically_relative(source);
            if (settings && relative == "RiceMonkey") {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory()) fs::create_directories(destination / relative);
            else if (it->is_regular_file() && it->path().filename() != ".gitkeep" &&
                     it->path().extension() != ".tmp" && it->path().extension() != ".bak")
                fs::copy_file(it->path(), destination / relative, fs::copy_options::overwrite_existing);
        }
    }
public:
    fs::path root;
    AssetDatabase assets;
    InputActions input;

    explicit Project(const fs::path& selected, bool requireSaved = false) : Project(Inspect(selected, requireSaved)) {}
    static fs::path ResolveSavedRoot(const fs::path& selected) { return Inspect(selected, true).root; }
    static fs::path CreateNew(const fs::path& selected) {
        if (selected.empty()) throw std::invalid_argument("Choose a new project name");
        auto path = fs::absolute(selected).lexically_normal();
        auto directory = path.extension() == ".rice" && !fs::is_directory(path) ? path.parent_path() : path;
        if (fs::exists(directory) && (!fs::is_directory(directory) || !fs::is_empty(directory)))
            throw std::runtime_error("Choose a new or empty project folder");
        Project project(path);
        Scene main;
        project.InitializeMainScene(main);
        ComponentRegistry registry;
        project.SaveScene(main, registry, project.ActiveScene());
        return project.FilePath();
    }
    const fs::path& FilePath() const { return file; }
    void SetDefaultSkybox(Scene& scene) const {
        if (defaultSkybox.empty()) throw std::runtime_error("The default Skybox/sky.png asset is missing");
        scene.settings.skybox = true;
        scene.settings.skyboxAsset = defaultSkybox;
    }
    void InitializeMainScene(Scene& scene) const {
        SetDefaultSkybox(scene);
        scene.settings.ground = true;
        scene.settings.groundCollision = true;
        scene.settings.grids = {0, 0, 1}; // XY, YZ, XZ
        scene.settings.axes = Vector3(1);

        auto& system = SceneLayers::CreateDefaults(scene);
        auto& sun = scene.CreateGameObject("Directional Light");
        sun.SetGroup(&system, false);
        sun.AddComponent<Light>().type = 1;
    }
    const fs::path& ActiveScene() const { return data.activeScene; }
    bool InputBindingsChanged() const { return input.keys != data.bindings; }
    bool HasScene(const fs::path& key) const { return data.scenes.contains(key); }
    Scope<Scene> LoadScene(ComponentRegistry& registry, const fs::path& key) const {
        auto found = data.scenes.find(key);
        if (found == data.scenes.end()) throw std::runtime_error("Scene does not exist in this project");
        auto scene = SceneCodec(registry).Decode(found->second);
        SceneLayers::Migrate(*scene);
        return scene;
    }
    Scope<Scene> LoadScene(ComponentRegistry& registry) const { return LoadScene(registry, ActiveScene()); }
    std::vector<fs::path> SceneFiles() const {
        std::vector<fs::path> scenes;
        for (const auto& [key, text] : data.scenes) scenes.push_back(key);
        return scenes;
    }
    static fs::path NamedScene(const std::string& name) {
        fs::path filename(std::u8string(name.begin(), name.end()));
        if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos ||
            filename.has_parent_path() || name.find_first_of("<>:\"/\\|?*") != std::string::npos)
            throw std::invalid_argument("Enter a scene name without path separators");
        if (filename.extension() != ".rice") filename += ".rice";
        return "Scenes" / filename;
    }

    void SaveActiveScene(const std::string& encoded) {
        auto next = data;
        next.bindings = input.keys;
        next.scenes[next.activeScene] = encoded;
        Commit(std::move(next));
    }
    void SaveInactiveScene(const fs::path& key, Scene& edited, ComponentRegistry& registry) {
        if (key == data.activeScene || !data.scenes.contains(key))
            throw std::invalid_argument("Choose a saved, inactive project scene");
        auto next = data;
        next.scenes.at(key) = SceneCodec(registry).Encode(edited);
        Commit(std::move(next));
    }
    void SaveScene(Scene& current, ComponentRegistry& registry, const fs::path& selected,
                   Scene* added = nullptr) {
        auto next = data;
        next.bindings = input.keys;
        SceneCodec codec(registry);
        next.scenes[next.activeScene] = codec.Encode(current);
        if (added && !next.scenes.emplace(selected, codec.Encode(*added)).second)
            throw std::runtime_error("A scene with that name already exists");
        if (!next.scenes.contains(selected)) throw std::runtime_error("Scene does not exist in this project");
        next.activeScene = selected;
        Commit(std::move(next));
    }

    void Export(Scene& current, ComponentRegistry& registry, const fs::path& binaries, const fs::path& destination) const {
        if (destination.empty()) throw std::invalid_argument("Choose a destination for the game build");
        auto target = fs::weakly_canonical(fs::absolute(destination));
        auto relative = target.lexically_relative(fs::weakly_canonical(root));
        if (!relative.empty() && *relative.begin() != "..")
            throw std::runtime_error("Export outside the project directory");
        if (fs::exists(target)) throw std::runtime_error("Choose a new build file or directory");
        auto executable = target;
        if (target.extension() != ".exe") {
            auto name = file.filename();
            executable = target / name.replace_extension(".exe");
        }
        auto host = binaries / "RiceGameHost.exe";
        auto player = binaries / "RicePlayer.exe";
        if (!fs::is_regular_file(host) || !fs::is_regular_file(player))
            throw std::runtime_error("The engine runtime is missing RiceGameHost.exe or RicePlayer.exe");

        ProjectFile packaged;
        packaged.activeScene = data.activeScene;
        packaged.bindings = input.keys;
        SceneCodec codec(registry);
        for (const auto& [key, text] : data.scenes) {
            if (key == packaged.activeScene) continue;
            auto scene = LoadScene(registry, key);
            ValidateAssets(*scene);
            packaged.scenes.emplace(key, codec.Encode(*scene, false));
        }
        ValidateAssets(current);
        packaged.scenes[packaged.activeScene] = codec.Encode(current, false);

        fs::create_directories(root / "Temp");
        auto stage = root / "Temp" / ("export-" + NewAssetID());
        if (!fs::create_directory(stage)) throw std::runtime_error("Cannot stage game build");
        std::error_code ignored;
        bool createdDirectory = false;
        try {
            auto project = stage / "Project";
            CopyContent(root / "Assets", project / "Assets");
            CopyContent(root / "Assets" / "Prefabs", project / "Prefabs");
            CopyContent(root / "Prefabs", project / "Prefabs");
            CopyContent(root / "Streaming", project / "Settings" / "Streaming");
            CopyContent(root / "Settings", project / "Settings", true);
            Scripting::ExportScripts(root, project);
            Write(project / file.filename(), packaged);
            fs::copy_file(player, stage / player.filename());
            bool includesFFmpeg = false;
            auto copyDll = [&](const fs::path& name) {
                if (name != name.filename() || name.has_root_path() || name.extension() != ".dll")
                    throw std::runtime_error("Invalid DLL filename in RicePlayer.dlls");
                fs::copy_file(binaries / name, stage / name);
                if (ProjectFile::Utf8(name.stem()).starts_with("avcodec")) includesFFmpeg = true;
            };
            if (fs::is_regular_file(binaries / "RicePlayer.dlls")) {
                std::ifstream dependencies(binaries / "RicePlayer.dlls");
                std::string name;
                while (std::getline(dependencies, name)) {
                    if (!name.empty() && name.back() == '\r') name.pop_back();
                    if (!name.empty()) copyDll(fs::path(std::u8string(name.begin(), name.end())));
                }
                if (!dependencies.eof()) throw std::runtime_error("Cannot read RicePlayer.dlls");
            } else {
                for (const auto& entry : fs::directory_iterator(binaries))
                    if (entry.is_regular_file() && entry.path().extension() == ".dll") copyDll(entry.path().filename());
            }
            if (fs::is_regular_file(binaries / "RicePlayer.control"))
                fs::copy_file(binaries / "RicePlayer.control", stage / "RicePlayer.control");
            if (fs::is_directory(binaries / "Licenses")) CopyContent(binaries / "Licenses", stage / "Licenses");
            if (includesFFmpeg && !fs::exists(stage / "Licenses/FFmpeg/LICENSE.txt")) {
                auto notices = InstallationRoot(binaries) / "Engine/ThirdParty/FFmpeg";
                if (!fs::is_regular_file(notices / "LICENSE.txt")) throw std::runtime_error("The runtime is missing its FFmpeg license");
                CopyContent(notices, stage / "Licenses/FFmpeg");
            }
            if (executable != target) createdDirectory = fs::create_directories(target);
            else fs::create_directories(target.parent_path());
            WriteGameBundle(host, stage, executable);
        } catch (...) {
            fs::remove_all(stage, ignored);
            if (createdDirectory) fs::remove(target, ignored);
            throw;
        }
        fs::remove_all(stage, ignored);
    }
};
}
