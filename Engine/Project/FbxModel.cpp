#include "Project/FbxModel.h"
#include "Project/ProjectFile.h"
#include <ufbx.h>
#include <string_view>

namespace Rice {
namespace {

std::string Text(ufbx_string text) {
    return std::string(text.data, text.length);
}

Vector3 Offset(ufbx_vec3 point, ufbx_vec3 origin) {
    Vector3 delta(float(point.x - origin.x), float(point.y - origin.y), float(point.z - origin.z));
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
        throw std::runtime_error("The FBX contains an invalid position.");
    return delta;
}

class ModelReader {
    std::vector<FbxPart> parts;
    std::unordered_map<const void*, std::shared_ptr<const std::string>> embeddedImages;
    size_t remainingCorners = ProjectFile::MaxSceneBytes / 6;

    const ufbx_texture* SetMaterial(FbxPart& part, const ufbx_material& material) {
        part.materialName = Text(material.name);
        const auto& pbr = material.pbr.base_color;
        const auto& diffuse = material.fbx.diffuse_color;
        const auto& tint = pbr.has_value ? pbr : diffuse;
        const auto& factor = pbr.has_value ? material.pbr.base_factor : material.fbx.diffuse_factor;
        if (tint.has_value) {
            const auto rgb = tint.value_vec3;
            const auto multiplier = factor.has_value ? factor.value_real : 1.0;
            part.color = Color(float(rgb.x * multiplier), float(rgb.y * multiplier), float(rgb.z * multiplier), 1.0f);
        }
        if (material.pbr.opacity.has_value) part.color.a = float(material.pbr.opacity.value_real);
        for (int i = 0; i < 4; ++i) {
            if (!std::isfinite(part.color[i])) throw std::runtime_error("The FBX contains an invalid material color.");
            part.color[i] = std::clamp(part.color[i], 0.0f, 1.0f);
        }
        auto* texture = pbr.texture && pbr.texture_enabled ? pbr.texture :
            (diffuse.texture_enabled ? diffuse.texture : nullptr);
        if (!texture || !texture->file_textures.count) return nullptr;
        auto* file = texture->file_textures.data[0];
        const auto name = file->relative_filename.length ? file->relative_filename : file->absolute_filename;
        if (name.length) part.albedoTexture = std::filesystem::path(std::u8string(name.data, name.data + name.length));
        if (file->content.size) {
            auto& data = embeddedImages[file->content.data];
            if (!data) data = std::make_shared<const std::string>(static_cast<const char*>(file->content.data), file->content.size);
            part.embeddedAlbedo = data;
        }
        return file;
    }

    void ReadMesh(FbxPart& part, const ufbx_node& node, const ufbx_mesh_part& group, ufbx_vec3 pivot) {
        const auto& mesh = *node.mesh;
        const auto* uv = &mesh.vertex_uv;
        const ufbx_texture* texture = nullptr;
        if (group.index < node.materials.count) texture = SetMaterial(part, *node.materials.data[group.index]);
        if (texture && texture->uv_set.length) {
            for (const auto& set : mesh.uv_sets)
                if (std::string_view(set.name.data, set.name.length) == std::string_view(texture->uv_set.data, texture->uv_set.length))
                    uv = &set.vertex_uv;
        }
        // These are the on-disk ShapeMesh limits. Report the offending node while
        // importing, before the project is changed.
        if (group.num_triangles > 200000)
            throw std::runtime_error("FBX mesh exceeds 200,000 triangles: " + part.name);
        auto& result = part.mesh;
        result.indices.reserve(group.num_triangles * 3);
        result.uv.reserve(group.num_triangles * 3);
        std::unordered_map<uint32_t, uint32_t> vertices;
        std::vector<uint32_t> corners;
        const bool mirrored = ufbx_matrix_determinant(&node.geometry_to_world) < 0;
        for (uint32_t faceId : group.face_indices) {
            if (mesh.face_hole.count && mesh.face_hole.data[faceId]) continue;
            const auto face = mesh.faces.data[faceId];
            if (face.num_indices < 3) continue;
            corners.resize(size_t(face.num_indices - 2) * 3);
            const auto triangles = ufbx_triangulate_face(corners.data(), corners.size(), &mesh, face);
            if (triangles == 0) throw std::runtime_error("Cannot triangulate an FBX polygon in " + part.name);
            // Each saved corner needs at least "0 0 0 " for its index and UV.
            // Bail out before expanding repeated mesh instances beyond the scene capacity.
            if (size_t(triangles) * 3 > remainingCorners) throw std::runtime_error("FBX meshes exceed the scene size limit.");
            remainingCorners -= size_t(triangles) * 3;
            for (size_t i = 0; i < size_t(triangles) * 3; i += 3) {
                if (mirrored) std::swap(corners[i + 1], corners[i + 2]);
                for (size_t k = i; k < i + 3; ++k) {
                    const auto corner = corners[k];
                    const auto vertex = mesh.vertex_indices.data[corner];
                    auto [it, inserted] = vertices.try_emplace(vertex, static_cast<uint32_t>(result.positions.size()));
                    if (inserted) {
                        if (result.positions.size() >= 100000) throw std::runtime_error("FBX mesh exceeds 100,000 vertices: " + part.name);
                        auto world = ufbx_transform_position(&node.geometry_to_world, mesh.vertices.data[vertex]);
                        result.positions.push_back(Offset(world, pivot));
                    }
                    result.indices.push_back(it->second);
                    auto texcoord = uv->exists ? ufbx_get_vertex_vec2(uv, corner) : ufbx_vec2{};
                    if (texture) {
                        auto mapped = ufbx_transform_position(&texture->uv_to_texture, {texcoord.x, texcoord.y, 0});
                        texcoord = {mapped.x, mapped.y};
                    }
                    result.uv.emplace_back(float(texcoord.x), float(texcoord.y));
                }
            }
        }
        result.Validate();
    }

    void AddNode(const ufbx_node& node, int parent, ufbx_vec3 parentPivot) {
        const int index = static_cast<int>(parts.size());
        auto pivot = node.node_to_world.cols[3];
        FbxPart part;
        part.name = node.name.length ? Text(node.name) : "Object";
        part.position = Offset(pivot, parentPivot);
        part.parent = parent;
        parts.push_back(std::move(part));
        if (node.mesh) {
            const auto& groups = node.mesh->material_parts;
            const auto count = std::count_if(groups.begin(), groups.end(), [](const auto& group) { return group.num_triangles > 0; });
            for (const auto& group : groups) {
                if (!group.num_triangles) continue;
                if (count == 1) {
                    ReadMesh(parts[index], node, group, pivot);
                } else {
                    FbxPart segment;
                    segment.parent = index;
                    segment.name = parts[index].name;
                    ReadMesh(segment, node, group, pivot);
                    if (segment.mesh.Empty()) continue;
                    segment.name += " - " + (segment.materialName.empty() ? std::to_string(group.index + 1) : segment.materialName);
                    parts.push_back(std::move(segment));
                }
            }
        }
        for (const auto* child : node.children) AddNode(*child, index, pivot);
        if (parts.size() == size_t(index + 1) && parts[index].mesh.Empty()) parts.pop_back();
    }

public:
    std::vector<FbxPart> Convert(const ufbx_scene& scene) {
        for (auto* child : scene.root_node->children) AddNode(*child, -1, {});
        if (parts.empty()) throw std::runtime_error("The FBX contains no polygon meshes.");
        return std::move(parts);
    }
};
}

std::vector<FbxPart> ReadFbx(const std::filesystem::path& path) {
    RICE_BENCHMARK_FUNCTION();
    ufbx_load_opts options{};
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1;
    options.handedness_conversion_axis = UFBX_MIRROR_AXIS_X;
    options.ignore_animation = true;
    options.skip_skin_vertices = true;
    options.node_depth_limit = 256;
    options.temp_allocator.memory_limit = 512ull * 1024 * 1024;
    options.result_allocator.memory_limit = 512ull * 1024 * 1024;
    ufbx_error error{};
    const auto filename = path.u8string();
    auto* raw = ufbx_load_file_len(reinterpret_cast<const char*>(filename.data()), filename.size(), &options, &error);
    if (!raw) throw std::runtime_error("Cannot import FBX: " + Text(error.description));
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(raw, &ufbx_free_scene);
    return ModelReader{}.Convert(*scene);
}
}
