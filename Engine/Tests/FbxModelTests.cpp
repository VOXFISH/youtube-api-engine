#include "Project/FbxModel.h"
#include <iostream>

using namespace Rice;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Vector3 RenderNormal(const std::vector<float>& vertices, size_t corner) {
    const size_t offset = corner * 8 + 3;
    return {vertices.at(offset), vertices.at(offset+1), vertices.at(offset+2)};
}

void CheckCornerNormals(const std::filesystem::path& fixture) {
    const auto parts = ReadFbx(fixture);
    Check(parts.size() == 1, "Corner normals fixture has a single mesh");
    const auto& mesh = parts[0].mesh;
    Check(mesh.positions.size() == 5 && mesh.indices.size() == 9 && mesh.normals.size() == 9,
        "Distinct FBX corner normals retain the shared position topology");
    auto rendered = mesh.RenderVertices();
    for (size_t corner = 0; corner < mesh.indices.size(); ++corner) {
        const auto p = mesh.positions[mesh.indices[corner]];
        const bool firstFace = corner < 6; // The first source polygon is a quad.
        Vector3 expected;
        if (glm::length(p) < 1e-5f) {
            // Same position on both faces, with distinct authored normals.
            expected = firstFace ? glm::normalize(Vector3(-.5f,0,1)) : Vector3(0,1,0);
        } else if (p.x < -1.f && p.y < .5f) {
            expected = glm::normalize(Vector3(0,1,1)); // Smooth shared normal.
        } else if (p.x < -1.f) {
            expected = Vector3(0,0,1);
        } else {
            // Missing index on the first face, zero-length normal on the second.
            expected = firstFace ? Vector3(0,0,1) : Vector3(0,1,0);
            Check(mesh.normals[corner] == Vector3(0), "Unusable FBX corner normals use the geometric fallback");
        }
        Check(glm::length(RenderNormal(rendered, corner) - expected) < 1e-5f,
            "Mirrored winding retains each corner's inverse-transposed shading normal");
    }
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
    Check(board.mesh.normals.empty(), "FBX geometry without normals retains automatic face normals");
    const auto boardVertices = board.mesh.RenderVertices();
    Check(board.mesh.faceGroups == std::vector<MeshFaceGroup>{{"Group 42",{0,1}}} && piece.mesh.faceGroups.empty(),
        "Triangulation preserves the FBX polygon group and leaves ungrouped geometry available for inference");
    for (size_t i = 0; i < board.mesh.indices.size(); ++i) {
        auto p = board.mesh.positions[board.mesh.indices[i]];
        Check(glm::length(board.mesh.uv[i] - Vector2(p.x, p.y)) < 1e-5f, "Corner UVs survive triangulation");
        Check(glm::length(RenderNormal(boardVertices,i) - Vector3(0,0,1)) < 1e-5f, "The board falls back to a perpendicular face normal");
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
    CheckCornerNormals(std::filesystem::path(argv[1]).parent_path() / "CornerNormals.fbx");
    std::cout << "FBX decode checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
