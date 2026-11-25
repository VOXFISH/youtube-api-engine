#pragma once

#include <filesystem>
#include <iomanip>
#include <istream>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>

namespace Rice {

// Scene names remain logical keys so that references from settings and older
// projects survive the move from separate scene files to one project document.
struct ProjectFile {
    using Path = std::filesystem::path;
    static constexpr std::size_t MaxSceneBytes = 16 * 1024 * 1024;
    std::map<std::string, int> bindings;
    Path activeScene = "main.rice";
    std::map<Path, std::string> scenes;

    static std::string Utf8(const Path& path) {
        auto text = path.generic_u8string();
        return {text.begin(), text.end()};
    }
    static Path SceneKey(const std::string& text) {
        Path key(std::u8string(text.begin(), text.end()));
        if (key.is_absolute() || key.extension() != ".rice" ||
            (key != "main.rice" && key.parent_path() != "Scenes"))
            throw std::runtime_error("Invalid scene name in project");
        return key;
    }

    static ProjectFile Read(std::istream& in, int& version) {
        ProjectFile project;
        std::string marker;
        int count;
        if (!(in >> marker >> version >> count) || marker != "RICE_PROJECT" ||
            version < 1 || version > 3 || count < 0 || count > 128)
            throw std::runtime_error("Invalid Rice project file");
        for (int i = 0; i < count; ++i) {
            std::string name;
            int key;
            if (!(in >> std::quoted(name) >> key) || key < 32 || key > 348)
                throw std::runtime_error("Invalid project input binding");
            project.bindings[name] = key;
        }
        if (version >= 2) {
            std::string scene;
            if (!(in >> marker >> std::quoted(scene)) || marker != "ActiveScene")
                throw std::runtime_error("Invalid active scene in project");
            project.activeScene = SceneKey(scene);
        }
        if (version == 3) {
            if (!(in >> marker >> count) || marker != "Scenes" || count <= 0)
                throw std::runtime_error("Invalid project scene list");
            for (int i = 0; i < count; ++i) {
                std::string key, scene;
                if (!(in >> marker >> std::quoted(key) >> std::quoted(scene)) || marker != "Scene" ||
                    scene.size() > MaxSceneBytes || !scene.starts_with("RICE_SCENE "))
                    throw std::runtime_error("Invalid embedded scene");
                if (!project.scenes.emplace(SceneKey(key), std::move(scene)).second)
                    throw std::runtime_error("Duplicate scene name in project");
            }
            if (!project.scenes.contains(project.activeScene))
                throw std::runtime_error("Missing active scene in project");
            in >> std::ws;
            if (!in.eof()) throw std::runtime_error("Unexpected data after project scenes");
        }
        return project;
    }

    void Write(std::ostream& out) const {
        if (!scenes.contains(activeScene)) throw std::logic_error("Cannot save project without active scene");
        out << "RICE_PROJECT 3 " << bindings.size() << '\n';
        for (const auto& [name, key] : bindings)
            out << std::quoted(name) << ' ' << key << '\n';
        out << "ActiveScene " << std::quoted(Utf8(activeScene)) << "\nScenes " << scenes.size() << '\n';
        for (const auto& [key, scene] : scenes) {
            if (scene.size() > MaxSceneBytes) throw std::runtime_error("Scene exceeds 16 MiB");
            out << "Scene " << std::quoted(Utf8(key)) << ' ' << std::quoted(scene) << '\n';
        }
        if (!out) throw std::runtime_error("Cannot write Rice project file");
    }
};
}
