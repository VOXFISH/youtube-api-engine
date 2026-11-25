#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Rice::Scripting {
struct PublishedModule {
    std::filesystem::path binary;
    std::shared_ptr<void> lease;
};
using ModuleCatalog = std::map<std::string, PublishedModule>;

// Project script links and publication keys are portable across editor and player.
std::filesystem::path ScriptSourcePath(const std::filesystem::path& project,const std::filesystem::path& link);
std::vector<std::filesystem::path> ScriptFiles(const std::filesystem::path& project);
std::filesystem::path ScriptModuleDirectory(const std::filesystem::path& project);
std::string ScriptKey(const std::filesystem::path& project, const std::filesystem::path& source);
ModuleCatalog PublishedScripts(const std::filesystem::path& project);
bool SourceMatchesPublication(const std::filesystem::path& project, const std::filesystem::path& sourceFile,
                              const std::string& source);
void ExportScripts(const std::filesystem::path& project, const std::filesystem::path& destination);
void CompileAndSave(const std::filesystem::path& project, const std::filesystem::path& destination,
                    const std::string& source);
void SaveValidatedScript(const std::filesystem::path& project, const std::filesystem::path& destination,
                         const std::string& source,const std::function<void(const std::string&)>& warn);
}
