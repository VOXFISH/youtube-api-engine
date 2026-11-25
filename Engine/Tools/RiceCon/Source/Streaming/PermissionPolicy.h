#pragma once
#include "YouTubeClient.h"
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace RiceController::Streaming {
enum class Permission { LocalAdmin, Everyone, Owner, OwnerOrModerator, ChannelAllowlist };
struct CommandPermission {
    Permission permission=Permission::LocalAdmin;
    std::set<std::string> allowedChannelIds;
};
class PermissionPolicy {
public:
    explicit PermissionPolicy(std::string liveChatId);
    void Register(std::string commandId,CommandPermission permission={});
    bool AllowsRemote(std::string_view commandId,const VerifiedAuthor& author)const;
    // Call only from the trusted Controller operator path, never from chat data.
    bool AllowsLocalOperator(std::string_view commandId)const;
    static bool Allows(const CommandPermission& permission,const VerifiedAuthor& author,std::string_view expectedLiveChatId);
private:
    std::string liveChatId_;
    std::map<std::string,CommandPermission,std::less<>> commands_;
};
}
