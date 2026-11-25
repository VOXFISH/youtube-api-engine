#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace RiceController {
struct Entry {
    std::string id, name;
    std::filesystem::path executable, project;
    std::string windowMode = "windowed";
};
std::string Utf8(const std::wstring& value);
std::wstring Wide(const std::string& value);
std::string PathText(const std::filesystem::path& value);
std::filesystem::path TextPath(const std::string& value);
std::string NewId();
std::string DefaultName(const std::vector<Entry>& entries);
void ValidateName(std::string& name);
void ValidateEntry(Entry& entry);
bool HasProject(const std::filesystem::path& project,std::error_code& error);
std::vector<Entry> LoadEntries(const std::filesystem::path& path);
void SaveEntries(const std::filesystem::path& path, const std::vector<Entry>& entries);
std::filesystem::path DefaultConfigPath();
std::wstring QuoteArgument(const std::wstring& value);
}
