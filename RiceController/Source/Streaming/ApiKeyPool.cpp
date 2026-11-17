#include "ApiKeyPool.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace RiceController::Streaming {
namespace {
void Identifier(std::string_view value) {
    if (value.empty() || value.size()>256 || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<33 || c==127;}))
        throw std::runtime_error("Invalid API credential metadata");
}
}
ApiKeyPool::ApiKeyPool(std::string project):projectId_(std::move(project)) { Identifier(projectId_); }
void ApiKeyPool::Add(ApiKeyEntry entry) {
    std::lock_guard lock(mutex_);
    Identifier(entry.keyId); Identifier(entry.projectId); Identifier(entry.credentialRef);
    if (entry.projectId!=projectId_) throw std::runtime_error("API keys in a pool must share one project quota group");
    if (entries_.size()>=100) throw std::runtime_error("Too many API key references");
    if (std::any_of(entries_.begin(),entries_.end(),[&](const ApiKeyEntry& key){return key.keyId==entry.keyId || key.credentialRef==entry.credentialRef;}))
        throw std::runtime_error("Duplicate API key reference");
    entries_.push_back(std::move(entry));
}
bool ApiKeyPool::SelectEnabled() {
    if (current_ && entries_[*current_].enabled) return true;
    for (std::size_t i=0;i<entries_.size();++i) if (entries_[i].enabled) { current_=i; return true; }
    current_.reset(); return false;
}
KeyDecision ApiKeyPool::Current() {
    std::lock_guard lock(mutex_);
    if (quotaExhausted_) return {KeyAction::Stop,{},"Project quota exhausted"};
    if (!SelectEnabled()) return {KeyAction::Stop,{},"No enabled API credential"};
    return {KeyAction::UseCurrent,entries_[*current_],{}};
}
KeyDecision ApiKeyPool::ReportFailure(std::string_view id,ApiKeyFailure failure) {
    std::lock_guard lock(mutex_);
    auto found=std::find_if(entries_.begin(),entries_.end(),[&](const ApiKeyEntry& key){return key.keyId==id;});
    if (found==entries_.end()) throw std::runtime_error("Unknown failed API key ID");
    if (failure==ApiKeyFailure::QuotaExceeded) {
        quotaExhausted_=true; return {KeyAction::Stop,{},"Project quota exhausted; changing keys cannot restore quota"};
    }
    if (quotaExhausted_) return {KeyAction::Stop,{},"Project quota exhausted"};
    if (failure==ApiKeyFailure::InvalidKey || failure==ApiKeyFailure::KeyRestricted) {
        found->enabled=false;
        bool replaced=!current_ || entries_[*current_].keyId==id;
        if (!SelectEnabled()) return {KeyAction::Stop,{},"No usable API credential after credential failure"};
        return {replaced ? KeyAction::UseReplacement : KeyAction::UseCurrent,entries_[*current_],"Credential rejected"};
    }
    if (failure==ApiKeyFailure::Network || failure==ApiKeyFailure::Server || failure==ApiKeyFailure::RateLimited) {
        if(!SelectEnabled())return {KeyAction::Stop,{},"No enabled API credential"};
        return {KeyAction::RetryLater,entries_[*current_],"Retry with backoff; keep the same credential"};
    }
    return {KeyAction::Stop,{},"Request or OAuth failure requires correction, not key rotation"};
}
void ApiKeyPool::SetEnabled(std::string_view id,bool enabled) {
    std::lock_guard lock(mutex_);
    auto found=std::find_if(entries_.begin(),entries_.end(),[&](const ApiKeyEntry& key){return key.keyId==id;});
    if (found==entries_.end()) throw std::runtime_error("Unknown API key ID");
    found->enabled=enabled;
    if (current_ && !entries_[*current_].enabled) current_.reset();
}
void ApiKeyPool::ConfirmQuotaReset() { std::lock_guard lock(mutex_);quotaExhausted_=false; }
bool ApiKeyPool::QuotaExhausted() const noexcept { std::lock_guard lock(mutex_);return quotaExhausted_; }
const std::string& ApiKeyPool::ProjectId() const noexcept { return projectId_; }
std::vector<ApiKeyEntry> ApiKeyPool::Entries() const { std::lock_guard lock(mutex_);return entries_; }
}
