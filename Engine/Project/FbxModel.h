#pragma once

#include "Scene/ShapeMesh.h"
#include <filesystem>
#include <memory>

namespace Rice {

// Parts are ordered parent first. Translations are parent-relative in meters,
// Y up. The source node's orientation and scale are baked into local geometry.
struct FbxPart {
    std::string name;
    int parent = -1;
    Vector3 position{0};
    ShapeMesh mesh;
    Color color{1};
    std::string materialName;
    std::filesystem::path albedoTexture;
    std::shared_ptr<const std::string> embeddedAlbedo;
};

std::vector<FbxPart> ReadFbx(const std::filesystem::path& path);

}
