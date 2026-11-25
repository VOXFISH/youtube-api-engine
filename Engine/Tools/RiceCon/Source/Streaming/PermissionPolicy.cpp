#include "PermissionPolicy.h"
#include <stdexcept>

namespace RiceController::Streaming {
PermissionPolicy::PermissionPolicy(std::string liveChatId):liveChatId_(std::move(liveChatId)) {
    if(liveChatId_.empty())throw std::runtime_error("Permission policy requires a live chat identity");
}
void PermissionPolicy::Register(std::string id,CommandPermission permission) {
    if(id.empty() || id.size()>512)throw std::runtime_error("Invalid command identity");
    switch(permission.permission) {
    case Permission::LocalAdmin:case Permission::Everyone:case Permission::Owner:case Permission::OwnerOrModerator:case Permission::ChannelAllowlist:break;
    default:throw std::runtime_error("Invalid permission policy");
    }
    for(const auto& channel:permission.allowedChannelIds)
        if(channel.empty() || channel.front()=='@')throw std::runtime_error("Allowlist requires stable YouTube channel IDs, not display names or handles");
    commands_.insert_or_assign(std::move(id),std::move(permission));
}
bool PermissionPolicy::Allows(const CommandPermission& rule,const VerifiedAuthor& author,std::string_view expectedChat) {
    if(!author.FromApiResponse() || author.ChannelId().empty() || expectedChat.empty() || author.LiveChatId()!=expectedChat)return false;
    switch(rule.permission) {
    case Permission::Everyone:return true;
    case Permission::Owner:return author.IsOwner();
    case Permission::OwnerOrModerator:return author.IsOwner() || author.IsModerator();
    case Permission::ChannelAllowlist:return rule.allowedChannelIds.contains(author.ChannelId());
    default:return false;
    }
}
bool PermissionPolicy::AllowsRemote(std::string_view id,const VerifiedAuthor& author)const {
    auto found=commands_.find(id);return found!=commands_.end() && Allows(found->second,author,liveChatId_);
}
bool PermissionPolicy::AllowsLocalOperator(std::string_view id)const {return commands_.find(id)!=commands_.end();}
}
