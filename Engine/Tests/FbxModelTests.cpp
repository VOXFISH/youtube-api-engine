#include "Project/FbxModel.h"
#include <iostream>

using namespace Rice;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Expected the TwoMeshes.fbx fixture.");
    const auto parts = ReadFbx(argv[1]);
    Check(parts.size() == 2, "The two FBX objects must stay separate");
    const auto& board = parts[0];
    const auto& piece = parts[1];
    Check(board.name == "Board" && board.parent == -1 && piece.name == "Piece" && piece.parent == 0,
        "Named FBX hierarchy");
    Check(glm::length(board.position - Vector3(2, 0, 0)) < 1e-5f &&
        glm::length(piece.position - Vector3(0, 1, 0)) < 1e-5f, "Centimeters to parent-local meters");
    Check(board.mesh.positions.size() == 4 && board.mesh.indices.size() == 6, "Quad triangulation with shared positions");
    for (size_t i = 0; i < board.mesh.indices.size(); ++i) {
        auto p = board.mesh.positions[board.mesh.indices[i]];
        Check(glm::length(board.mesh.uv[i] - Vector2(p.x, p.y)) < 1e-5f, "Corner UVs survive triangulation");
    }
    Check(piece.materialName == "Ivory" && glm::length(piece.color - Color(.8f, .7f, .6f, 1)) < 1e-5f,
        "FBX diffuse material");
    auto [low, high] = piece.mesh.Bounds();
    Check(glm::length(low - Vector3(-2, 0, 0)) < 1e-5f && glm::length(high - Vector3(0, 1, 0)) < 1e-5f,
        "Node scale baked into pivot-relative positions");
    auto at = [&](size_t i) { return piece.mesh.positions.at(piece.mesh.indices.at(i)); };
    Check(glm::cross(at(1) - at(0), at(2) - at(0)).z > 0, "Mirrored instances retain front-facing triangles");
    ShapeMesh detail;
    detail.positions={{0,0,0},{0,0,.00005f},{.00005f,0,0}};
    detail.indices={0,1,2};detail.uv.resize(3);
    const auto rendered=detail.RenderVertices();
    Check(rendered[4]>.99f, "Sub-millimeter mesh details retain their upward normal");
    std::cout << "FBX decode checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
