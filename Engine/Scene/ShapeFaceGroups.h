#pragma once
#include "ShapeMesh.h"
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace Rice {
struct ShapeFaceGroups {
    struct FaceGroup {
        std::string name;
        std::vector<uint32_t> triangles;
        std::vector<uint32_t> vertices;
    };
    std::vector<FaceGroup> groups;
    std::vector<uint32_t> triangleToGroup;

    static ShapeFaceGroups Build(const ShapeMesh& mesh) {
        ShapeFaceGroups result;
        const auto triangleCount = static_cast<uint32_t>(mesh.indices.size() / 3);
        result.triangleToGroup.assign(triangleCount, Unassigned);
        size_t authoredTriangles = 0;
        std::unordered_set<std::string> authoredNames;
        for (const auto& authored : mesh.faceGroups) {
            auto group = static_cast<uint32_t>(result.groups.size());
            result.groups.push_back({authored.name});
            // The tree clips by fixed-height rows. Keep the original text in
            // the mesh while presenting imported names on a single line.
            auto& name = result.groups.back().name;
            std::replace_if(name.begin(), name.end(), [](char c){ return c=='\n' || c=='\r' || c=='\0'; }, ' ');
            authoredNames.insert(name);
            for (auto triangle : authored.triangles) result.triangleToGroup[triangle] = group;
            authoredTriangles += authored.triangles.size();
        }
        if (authoredTriangles < triangleCount) result.GroupCoplanarFaces(mesh);

        // Mesh order is also the order used by rendering and per-triangle overrides.
        for (uint32_t t = 0; t < triangleCount; ++t) {
            auto& group = result.groups[result.triangleToGroup[t]];
            group.triangles.push_back(t);
            group.vertices.insert(group.vertices.end(), mesh.indices.begin() + t*3, mesh.indices.begin() + t*3 + 3);
        }
        size_t nextNumber = 0;
        for (size_t i = 0; i < result.groups.size(); ++i) {
            auto& group = result.groups[i];
            if (group.name.empty()) {
                nextNumber = std::max(i,nextNumber);
                do { group.name = "Group " + std::to_string(nextNumber++); } while (authoredNames.contains(group.name));
            }
            std::sort(group.vertices.begin(), group.vertices.end());
            group.vertices.erase(std::unique(group.vertices.begin(), group.vertices.end()), group.vertices.end());
        }
        return result;
    }

private:
    static constexpr uint32_t Unassigned = std::numeric_limits<uint32_t>::max();

    void GroupCoplanarFaces(const ShapeMesh& mesh) {
        const auto count = static_cast<uint32_t>(triangleToGroup.size());
        std::vector<glm::dvec3> normals(count);
        struct EdgeFaces { uint32_t first, second; bool nonManifold; };
        std::unordered_map<uint64_t, EdgeFaces> edges;
        for (uint32_t t = 0; t < count; ++t) {
            if (triangleToGroup[t] != Unassigned) continue;
            const auto* corner = mesh.indices.data() + 3*t;
            const glm::dvec3 a(mesh.positions[corner[0]]), b(mesh.positions[corner[1]]), c(mesh.positions[corner[2]]);
            const auto cross = glm::cross(b-a, c-a);
            if (glm::dot(cross,cross) == 0) continue;
            normals[t] = glm::normalize(cross);
            for (int i = 0; i < 3; ++i) {
                auto [low,high] = std::minmax(corner[i], corner[(i+1)%3]);
                const uint64_t key = (uint64_t(low) << 32) | high;
                auto [it,added] = edges.try_emplace(key, EdgeFaces{t,Unassigned,false});
                if (!added) {
                    if (it->second.second == Unassigned) it->second.second = t;
                    else it->second.nonManifold = true;
                }
            }
        }
        std::vector<uint32_t> parent(count);
        std::iota(parent.begin(), parent.end(), 0u);
        auto root = [&](uint32_t t) {
            while (parent[t] != t) { parent[t] = parent[parent[t]]; t = parent[t]; }
            return t;
        };
        for (const auto& [key,edge] : edges) {
            if (edge.nonManifold || edge.second == Unassigned) continue;
            // An indexed edge already places both triangles on the same line.
            // Compare unit normals so the tolerance is independent of mesh scale.
            if (std::abs(glm::dot(normals[edge.first], normals[edge.second])) >= 1.0 - 1e-8)
                parent[root(edge.first)] = root(edge.second);
        }
        std::vector<uint32_t> clusterGroup(count, Unassigned);
        for (uint32_t t = 0; t < count; ++t) {
            if (triangleToGroup[t] != Unassigned) continue;
            auto& group = clusterGroup[root(t)];
            if (group == Unassigned) {
                group = static_cast<uint32_t>(groups.size());
                groups.emplace_back();
            }
            triangleToGroup[t] = group;
        }
    }
};
}
