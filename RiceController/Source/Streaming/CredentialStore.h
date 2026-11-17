#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RiceController::Streaming {
// DPAPI CurrentUser encrypted files. References are names, never filesystem paths.
// No credential enumeration or access to browser/Windows account credentials.
class CredentialStore {
public:
    explicit CredentialStore(std::filesystem::path directory);
    void Put(std::string_view reference,std::string_view secret);
    std::optional<std::string> Get(std::string_view reference) const;
    bool Erase(std::string_view reference);
private:
    std::filesystem::path directory_;
    std::filesystem::path File(std::string_view reference) const;
};
// Redacts explicit secret values plus common URL/header credential fields.
std::string Redact(std::string text,const std::vector<std::string>& secrets = {});
}
