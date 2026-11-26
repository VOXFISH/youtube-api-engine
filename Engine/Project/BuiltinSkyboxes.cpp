#include "Assets.h"
#include <windows.h>

namespace Rice {
// Installed and build-tree executables both have their asset library one directory up.
static fs::path SkyboxLibrary() {
    std::wstring executable(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!count || count >= executable.size()) throw std::runtime_error("Cannot locate the Rice skybox library");
    executable.resize(count);
    return fs::path(executable).parent_path().parent_path() / "Assets" / "Skybox";
}

std::string InstallBuiltinSkyboxes(AssetDatabase& assets) {
    const auto library = SkyboxLibrary();
    const auto folder = assets.Root() / "Skybox";
    // Standalone games already carry their project assets and have no editor library.
    if (fs::is_directory(library)) {
        fs::create_directories(folder);
        bool added = false;
        for (const auto* name : {"sf.png", "sky.png", "night.png"}) {
            if (fs::is_regular_file(folder / name)) continue;
            if (fs::copy_file(library / name, folder / name, fs::copy_options::skip_existing)) added = true;
        }
        if (added) assets.Refresh();
    }
    for (const auto& entry : assets.Entries())
        if (entry.path == "Skybox/sky.png") return entry.id;
    return {};
}
}
