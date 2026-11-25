#pragma once
#include "Rice/Core.h"
#include <filesystem>
#include <optional>

namespace Rice {
// nullopt means the user cancelled. Dialog failures are reported as exceptions.
RICE_API std::optional<std::filesystem::path> PickProjectFile(const std::filesystem::path& directory);
RICE_API std::optional<std::filesystem::path> PickAssetFile();
RICE_API std::optional<std::filesystem::path> PickShapeFile(bool save,const std::filesystem::path& suggested);
RICE_API std::optional<std::filesystem::path> PickScriptEditor();
RICE_API std::optional<std::filesystem::path> PickScriptFile(const std::filesystem::path& directory);
RICE_API std::optional<std::filesystem::path> PickScriptDestination(const std::filesystem::path& suggested,bool preset=false);
RICE_API void OpenScriptFile(const std::filesystem::path& script,const std::filesystem::path& editor);
}
