#pragma once
#include <filesystem>

namespace Rice {
inline std::filesystem::path InstallationRoot(const std::filesystem::path& binaries) {
    auto directory = std::filesystem::absolute(binaries).lexically_normal();
    if (directory.filename() == "Runtime" && directory.parent_path().filename() == "Engine")
        return directory.parent_path().parent_path();
    for (auto parent = directory; parent != parent.root_path(); parent = parent.parent_path()) {
        if (std::filesystem::is_directory(parent / "Projects") &&
            std::filesystem::is_directory(parent / "Engine")) return parent;
    }
    return directory;
}
}
