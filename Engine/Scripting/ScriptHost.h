#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace Rice {
class Scene;
class PhysicsSystem;
class ComponentRegistry;
namespace Scripting {
void AttachNativeScripts(Scene& scene, ComponentRegistry& registry, PhysicsSystem& physics,
                         const std::filesystem::path& project, bool editorPreview,
                         std::function<void(int, const std::string&)> log,
                         std::function<void(const std::string&,std::uint64_t,const std::string&)> runtimeDiagnostic = {});
}
}
