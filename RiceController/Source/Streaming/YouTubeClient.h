#pragma once
#include "ApiKeyPool.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace RiceController::Streaming {
enum class YouTubeErrorKind { InvalidRequest, CredentialUnavailable, InvalidKey, KeyRestricted, Unauthorized,
    Forbidden, QuotaExceeded, RateLimited, ChatEnded, ChatDisabled, NotFound, Network, Timeout, Cancelled, InvalidResponse, Server };
class YouTubeError:public std::runtime_error {
public:
    YouTubeErrorKind kind;
    int httpStatus=0;
    int64_t retryAfterMillis=0;
    YouTubeError(YouTubeErrorKind kind,std::string message,int status=0,int64_t retryAfter=0);
};
struct HttpRequest {
    std::string path; // Only a relative /youtube/v3/ path on www.googleapis.com.
    std::vector<std::pair<std::string,std::string>> headers;
    int timeoutMillis=15000;
    size_t maximumResponseBytes=4*1024*1024;
    std::function<bool()> cancelled;
};
struct HttpResponse { int status=0;std::string body;int64_t retryAfterMillis=0; };
class HttpTransport {
public:
    virtual ~HttpTransport()=default;
    virtual HttpResponse Get(const HttpRequest& request)=0;
};
// Worker-facing blocking interface; WinHTTP runs asynchronously internally so
// the overall deadline and cancellation can safely close pending requests.
class WinHttpTransport final:public HttpTransport {
public:
    HttpResponse Get(const HttpRequest& request) override;
};
class VerifiedAuthor {
public:
    VerifiedAuthor()=default; // Unverified/empty; permission checks reject it.
    const std::string& ChannelId()const{return channelId_;}
    const std::string& DisplayName()const{return displayName_;}
    const std::string& LiveChatId()const{return liveChatId_;}
    bool IsOwner()const{return owner_;}
    bool IsModerator()const{return moderator_;}
    bool IsMember()const{return member_;}
    bool IsVerifiedByYouTube()const{return verified_;}
    bool FromApiResponse()const{return fromApi_;}
private:
    friend class YouTubeClient;
    std::string channelId_,displayName_,liveChatId_;
    bool owner_=false,moderator_=false,member_=false,verified_=false,fromApi_=false;
};
struct ChatMessage {
    std::string id,liveChatId,type,publishedAt,text;
    VerifiedAuthor author;
    bool IsTextCommandCandidate()const{return type=="textMessageEvent" && author.FromApiResponse();}
};
struct VideoInfo {std::string videoId,channelId,title,activeLiveChatId;};
struct ChatPoll {
    bool deferred=false,initialHistory=false;
    uint64_t generation=0;
    int64_t nextPollMillis=0;
    std::string offlineAt;
    std::vector<ChatMessage> messages;
};
struct YouTubeAuthentication {
    ApiKeyPool* apiKeys=nullptr;
    std::string oauthAccessTokenRef; // Supplied token only; no OAuth sign-in/refresh implementation.
};
class YouTubeClient {
public:
    using SecretReader=std::function<std::optional<std::string>(std::string_view)>;
    using Clock=std::function<int64_t()>;
    YouTubeClient(std::shared_ptr<HttpTransport> transport,SecretReader secrets,YouTubeAuthentication auth,Clock clock={});
    VideoInfo ReadVideo(std::string_view videoId);
    // All except Cancel() are called serially by one worker.
    void BeginChat(std::string liveChatId,bool discardInitialHistory=true);
    void Cancel() noexcept;
    uint64_t Generation()const noexcept;
    ChatPoll Poll(bool discardMessages=false);
    const std::string& PageToken()const{return pageToken_;}
    int64_t NextPollMillis()const{return nextPoll_;}
private:
    std::shared_ptr<HttpTransport> transport_;
    SecretReader secrets_;
    YouTubeAuthentication auth_;
    Clock clock_;
    std::shared_ptr<std::atomic<uint64_t>> generation_;
    std::string chatId_,pageToken_;
    bool discardHistory_=true,first_=true;
    uint64_t chatGeneration_=0;
    int64_t nextPoll_=0;
    unsigned failures_=0;
    std::set<std::string> seen_;
    std::deque<std::string> seenOrder_;
    HttpResponse Request(std::string path,uint64_t generation);
};
std::string UrlEncode(std::string_view text);
}
