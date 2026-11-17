#pragma once
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace RiceController::Streaming {
struct ApiKeyEntry {
    std::string keyId, projectId, credentialRef;
    bool enabled = true;
};
enum class ApiKeyFailure { InvalidKey, KeyRestricted, QuotaExceeded, RateLimited, Network, Server, Unauthorized, Other };
enum class KeyAction { UseCurrent, UseReplacement, RetryLater, Stop };
struct KeyDecision {
    KeyAction action = KeyAction::Stop;
    std::optional<ApiKeyEntry> key;
    std::string reason;
};

// One pool is one Google Cloud project/quota group. Only credential-specific
// failures rotate a key. Project quota exhaustion stops the whole pool.
// Stores credential references only, never the key value.
class ApiKeyPool {
public:
    explicit ApiKeyPool(std::string projectId);
    void Add(ApiKeyEntry entry);
    KeyDecision Current();
    KeyDecision ReportFailure(std::string_view keyId, ApiKeyFailure failure);
    void SetEnabled(std::string_view keyId, bool enabled);
    // Invoke after confirming the project's quota reset, not as automatic retry.
    void ConfirmQuotaReset();
    bool QuotaExhausted() const noexcept;
    const std::string& ProjectId() const noexcept;
    std::vector<ApiKeyEntry> Entries() const;
private:
    mutable std::mutex mutex_;
    std::string projectId_;
    std::vector<ApiKeyEntry> entries_;
    std::optional<std::size_t> current_;
    bool quotaExhausted_ = false;
    bool SelectEnabled();
};
}
