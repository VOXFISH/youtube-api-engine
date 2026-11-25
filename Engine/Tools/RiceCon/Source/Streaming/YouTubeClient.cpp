#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#include "YouTubeClient.h"
#include "../../Shared/Json.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <utility>

namespace RiceController::Streaming {
namespace {
using Steady=std::chrono::steady_clock;
int64_t Millis() {return std::chrono::duration_cast<std::chrono::milliseconds>(Steady::now().time_since_epoch()).count();}
void Identifier(std::string_view value,const char* name) {
    if(value.empty() || value.size()>1024 || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<33 || c==127;}))
        throw YouTubeError(YouTubeErrorKind::InvalidRequest,std::string("Invalid ")+name);
}
std::string String(const Json& object,const char* field) {auto value=object.Find(field);return value?value->String():std::string{};}
bool Boolean(const Json& object,const char* field) {auto value=object.Find(field);return value?value->Boolean():false;}
std::wstring Ascii(std::string_view value) {
    if(std::any_of(value.begin(),value.end(),[](unsigned char c){return c>127 || c=='\r' || c=='\n' || c==0;}))
        throw YouTubeError(YouTubeErrorKind::InvalidRequest,"HTTP input must be ASCII without control separators");
    return {value.begin(),value.end()};
}
struct Internet {
    HINTERNET value=nullptr;
    explicit Internet(HINTERNET handle=nullptr):value(handle){}
    ~Internet(){if(value)WinHttpCloseHandle(value);}
    Internet(const Internet&)=delete;
    Internet& operator=(const Internet&)=delete;
};
struct AsyncState {
    std::mutex mutex;
    std::condition_variable changed;
    DWORD completed=0,error=0,readBytes=0;
    std::array<char,16384> buffer{}; // Retained until HANDLE_CLOSING, including cancellation.
    std::wstring headers;
};
using CallbackOwner=std::shared_ptr<AsyncState>;
void CALLBACK Callback(HINTERNET,DWORD_PTR context,DWORD status,LPVOID info,DWORD length) {
    if(!context)return;
    auto* owner=reinterpret_cast<CallbackOwner*>(context);
    auto state=*owner;
    if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){delete owner;return;}
    std::lock_guard lock(state->mutex);
    if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)state->error=static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
    else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE || status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        state->completed=status;if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE)state->readBytes=length;
    }
    state->changed.notify_all();
}
[[noreturn]] void NetworkError(DWORD code) {
    throw YouTubeError(code==ERROR_WINHTTP_TIMEOUT?YouTubeErrorKind::Timeout:YouTubeErrorKind::Network,
        "YouTube HTTPS transport failed (Windows error "+std::to_string(code)+")");
}
void CheckStart(BOOL started) {if(!started && GetLastError()!=ERROR_IO_PENDING)NetworkError(GetLastError());}
void Reset(AsyncState& state) {std::lock_guard lock(state.mutex);state.completed=0;state.error=0;state.readBytes=0;}
void Wait(AsyncState& state,DWORD expected,const HttpRequest& request,Steady::time_point deadline) {
    std::unique_lock lock(state.mutex);
    for(;;) {
        if(request.cancelled && request.cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"YouTube request cancelled");
        if(Steady::now()>=deadline)throw YouTubeError(YouTubeErrorKind::Timeout,"YouTube request exceeded its total deadline");
        if(state.error)NetworkError(state.error);
        if(state.completed==expected)return;
        state.changed.wait_until(lock,std::min(deadline,Steady::now()+std::chrono::milliseconds(25)));
    }
}
YouTubeError ApiFailure(const HttpResponse& response) {
    std::string reason;
    try {
        auto document=Json::Parse(response.body);const auto& error=document.At("error");
        if(auto errors=error.Find("errors");errors && !errors->Items().empty())reason=String(errors->Items().front(),"reason");
        if(reason.empty())if(auto details=error.Find("details"))for(const auto& detail:details->Items())if(!String(detail,"reason").empty()){reason=String(detail,"reason");break;}
    }catch(...){}
    auto kind=YouTubeErrorKind::InvalidRequest;
    if(reason=="keyInvalid" || reason=="keyExpired" || reason=="API_KEY_INVALID")kind=YouTubeErrorKind::InvalidKey;
    else if(reason=="ipRefererBlocked" || reason=="API_KEY_HTTP_REFERRER_BLOCKED" || reason=="API_KEY_IP_ADDRESS_BLOCKED" || reason=="API_KEY_SERVICE_BLOCKED")kind=YouTubeErrorKind::KeyRestricted;
    else if(reason=="quotaExceeded" || reason=="dailyLimitExceeded" || reason=="dailyLimitExceededUnreg")kind=YouTubeErrorKind::QuotaExceeded;
    else if(reason=="rateLimitExceeded" || reason=="userRateLimitExceeded" || response.status==429)kind=YouTubeErrorKind::RateLimited;
    else if(reason=="liveChatEnded")kind=YouTubeErrorKind::ChatEnded;
    else if(reason=="liveChatDisabled")kind=YouTubeErrorKind::ChatDisabled;
    else if(response.status==401)kind=YouTubeErrorKind::Unauthorized;
    else if(response.status==403)kind=YouTubeErrorKind::Forbidden;
    else if(response.status==404)kind=YouTubeErrorKind::NotFound;
    else if(response.status>=500)kind=YouTubeErrorKind::Server;
    // Never surface the provider's freeform error message, URL, or credentials.
    return {kind,"YouTube API rejected the request (HTTP "+std::to_string(response.status)+", category "+std::to_string(static_cast<int>(kind))+")",
        response.status,response.retryAfterMillis};
}
ApiKeyFailure PoolFailure(YouTubeErrorKind kind) {
    switch(kind) {
    case YouTubeErrorKind::InvalidKey:return ApiKeyFailure::InvalidKey;
    case YouTubeErrorKind::KeyRestricted:return ApiKeyFailure::KeyRestricted;
    case YouTubeErrorKind::QuotaExceeded:return ApiKeyFailure::QuotaExceeded;
    case YouTubeErrorKind::RateLimited:return ApiKeyFailure::RateLimited;
    case YouTubeErrorKind::Network:case YouTubeErrorKind::Timeout:return ApiKeyFailure::Network;
    case YouTubeErrorKind::Server:return ApiKeyFailure::Server;
    case YouTubeErrorKind::Unauthorized:return ApiKeyFailure::Unauthorized;
    default:return ApiKeyFailure::Other;
    }
}
bool Retryable(YouTubeErrorKind kind) {
    return kind==YouTubeErrorKind::Network || kind==YouTubeErrorKind::Timeout || kind==YouTubeErrorKind::Server || kind==YouTubeErrorKind::RateLimited;
}
}
YouTubeError::YouTubeError(YouTubeErrorKind value,std::string message,int status,int64_t retryAfter)
    :std::runtime_error(std::move(message)),kind(value),httpStatus(status),retryAfterMillis(retryAfter){}
std::string UrlEncode(std::string_view text) {
    constexpr char hex[]="0123456789ABCDEF";std::string result;
    for(unsigned char c:text) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')result+=static_cast<char>(c);
        else {result+='%';result+=hex[c>>4];result+=hex[c&15];}
    }
    return result;
}
HttpResponse WinHttpTransport::Get(const HttpRequest& input) {
    if(input.timeoutMillis<1 || input.timeoutMillis>15000 || input.maximumResponseBytes>4*1024*1024 || !input.maximumResponseBytes)
        throw YouTubeError(YouTubeErrorKind::InvalidRequest,"Invalid HTTP request bounds");
    if(!input.path.starts_with("/youtube/v3/") || input.path.find('#')!=std::string::npos)
        throw YouTubeError(YouTubeErrorKind::InvalidRequest,"Only YouTube Data API paths are allowed");
    if(input.cancelled && input.cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"YouTube request cancelled");
    const auto deadline=Steady::now()+std::chrono::milliseconds(input.timeoutMillis);
    Internet session(WinHttpOpen(L"RiceController/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
    if(!session.value)NetworkError(GetLastError());
    if(!WinHttpSetTimeouts(session.value,input.timeoutMillis,input.timeoutMillis,input.timeoutMillis,input.timeoutMillis))NetworkError(GetLastError());
    Internet connection(WinHttpConnect(session.value,L"www.googleapis.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    if(!connection.value)NetworkError(GetLastError());
    auto path=Ascii(input.path);
    Internet request(WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!request.value)NetworkError(GetLastError());
    DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_REDIRECTS;
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)))NetworkError(GetLastError());
    auto state=std::make_shared<AsyncState>();
    auto owner=std::make_unique<CallbackOwner>(state);
    DWORD_PTR context=reinterpret_cast<DWORD_PTR>(owner.get());
    if(!WinHttpSetOption(request.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)))NetworkError(GetLastError());
    if(WinHttpSetStatusCallback(request.value,Callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
        NetworkError(GetLastError());
    owner.release(); // Callback releases this on the final HANDLE_CLOSING notification.
    state->headers=L"Accept: application/json\r\n";
    for(const auto& [name,value]:input.headers) {
        if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-")!=std::string::npos)
            throw YouTubeError(YouTubeErrorKind::InvalidRequest,"Invalid HTTP header name");
        state->headers+=Ascii(name)+L": "+Ascii(value)+L"\r\n";
    }
    Reset(*state);CheckStart(WinHttpSendRequest(request.value,state->headers.c_str(),static_cast<DWORD>(state->headers.size()),WINHTTP_NO_REQUEST_DATA,0,0,context));
    Wait(*state,WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,input,deadline);
    Reset(*state);CheckStart(WinHttpReceiveResponse(request.value,nullptr));Wait(*state,WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,input,deadline);
    HttpResponse response;DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))NetworkError(GetLastError());
    response.status=static_cast<int>(status);
    wchar_t retry[64]{};size=sizeof(retry);
    if(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_CUSTOM,L"Retry-After",retry,&size,WINHTTP_NO_HEADER_INDEX)) {
        std::wstring value(retry);if(!value.empty() && value.find_first_not_of(L"0123456789")==std::wstring::npos)
            try {response.retryAfterMillis=std::min<int64_t>(std::stoll(value),86400)*1000;}catch(...){}
    }
    for(;;) {
        Reset(*state);CheckStart(WinHttpReadData(request.value,state->buffer.data(),static_cast<DWORD>(state->buffer.size()),nullptr));
        Wait(*state,WINHTTP_CALLBACK_STATUS_READ_COMPLETE,input,deadline);
        DWORD read=0;{std::lock_guard lock(state->mutex);read=state->readBytes;}
        if(!read)break;
        if(read>state->buffer.size() || response.body.size()+read>input.maximumResponseBytes)
            throw YouTubeError(YouTubeErrorKind::InvalidResponse,"YouTube response exceeds configured size limit");
        response.body.append(state->buffer.data(),read);
    }
    return response;
}
YouTubeClient::YouTubeClient(std::shared_ptr<HttpTransport> transport,SecretReader secrets,YouTubeAuthentication auth,Clock clock)
    :transport_(std::move(transport)),secrets_(std::move(secrets)),auth_(std::move(auth)),clock_(clock?std::move(clock):Clock(Millis)),generation_(std::make_shared<std::atomic<uint64_t>>(1)) {
    if(!transport_ || !secrets_)throw YouTubeError(YouTubeErrorKind::InvalidRequest,"Transport and credential reader are required");
}
void YouTubeClient::Cancel()noexcept {generation_->fetch_add(1,std::memory_order_relaxed);}
uint64_t YouTubeClient::Generation()const noexcept{return generation_->load(std::memory_order_relaxed);}
void YouTubeClient::BeginChat(std::string id,bool discardHistory) {
    Identifier(id,"live chat ID");Cancel();chatGeneration_=Generation();chatId_=std::move(id);pageToken_.clear();
    seen_.clear();seenOrder_.clear();discardHistory_=discardHistory;first_=true;nextPoll_=0;failures_=0;
}
HttpResponse YouTubeClient::Request(std::string path,uint64_t generation) {
    const auto deadline=clock_()+15000;
    const auto sharedGeneration=generation_;
    auto cancelled=[sharedGeneration,generation]{return sharedGeneration->load(std::memory_order_relaxed)!=generation;};
    const auto maximumAttempts=auth_.oauthAccessTokenRef.empty()&&auth_.apiKeys?auth_.apiKeys->Entries().size()+1:1;
    for(size_t attempt=0;attempt<maximumAttempts;++attempt) {
        if(cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"Stale YouTube request generation");
        std::optional<ApiKeyEntry> key;std::string reference=auth_.oauthAccessTokenRef;
        if(reference.empty()) {
            if(!auth_.apiKeys)throw YouTubeError(YouTubeErrorKind::CredentialUnavailable,"Provide an API key or OAuth access token reference");
            auto selected=auth_.apiKeys->Current();
            if(!selected.key)throw YouTubeError(auth_.apiKeys->QuotaExhausted()?YouTubeErrorKind::QuotaExceeded:YouTubeErrorKind::CredentialUnavailable,"No usable API credential in this project");
            key=std::move(selected.key);reference=key->credentialRef;
        }
        std::optional<std::string> secret;
        try{secret=secrets_(reference);}catch(...){throw YouTubeError(YouTubeErrorKind::CredentialUnavailable,"Unable to read protected API credential");}
        if(!secret || secret->empty() || secret->size()>65536 || secret->find_first_of("\r\n")!=std::string::npos || secret->find('\0')!=std::string::npos)
            throw YouTubeError(YouTubeErrorKind::CredentialUnavailable,"API credential is missing or invalid");
        const auto remaining=deadline-clock_();
        if(remaining<=0)throw YouTubeError(YouTubeErrorKind::Timeout,"API operation exceeded its total deadline");
        HttpRequest request;request.path=path;request.cancelled=cancelled;
        request.timeoutMillis=static_cast<int>(remaining);
        if(key)request.path+="&key="+UrlEncode(*secret);else request.headers.emplace_back("Authorization","Bearer "+*secret);
        SecureZeroMemory(secret->data(),secret->size());secret.reset();
        HttpResponse response;
        try{response=transport_->Get(request);}catch(const YouTubeError& error) {
            if(cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"Stale YouTube request generation");
            if(key)auth_.apiKeys->ReportFailure(key->keyId,PoolFailure(error.kind));
            throw YouTubeError(error.kind,"YouTube transport operation failed",error.httpStatus,error.retryAfterMillis);
        }catch(...) {
            if(cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"Stale YouTube request generation");
            if(key)auth_.apiKeys->ReportFailure(key->keyId,ApiKeyFailure::Network);
            throw YouTubeError(YouTubeErrorKind::Network,"YouTube transport operation failed");
        }
        if(cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"Discarded stale YouTube response");
        if(clock_()>=deadline)throw YouTubeError(YouTubeErrorKind::Timeout,"Discarded expired YouTube response");
        if(response.body.size()>request.maximumResponseBytes)throw YouTubeError(YouTubeErrorKind::InvalidResponse,"YouTube response exceeds configured size limit");
        if(response.status>=200 && response.status<300)return response;
        auto error=ApiFailure(response);
        if(key) {
            auto decision=auth_.apiKeys->ReportFailure(key->keyId,PoolFailure(error.kind));
            if((error.kind==YouTubeErrorKind::InvalidKey || error.kind==YouTubeErrorKind::KeyRestricted) &&
                decision.key && decision.key->keyId!=key->keyId)continue;
        }
        throw error;
    }
    throw YouTubeError(YouTubeErrorKind::CredentialUnavailable,"No usable replacement API credential");
}
VideoInfo YouTubeClient::ReadVideo(std::string_view id) {
    Identifier(id,"video ID");const auto generation=Generation();
    auto response=Request("/youtube/v3/videos?part=snippet%2CliveStreamingDetails&id="+UrlEncode(id)+
        "&fields=items(id%2Csnippet(channelId%2Ctitle)%2CliveStreamingDetails(activeLiveChatId))",generation);
    try {
        auto document=Json::Parse(response.body);const auto& items=document.At("items").Items();
        if(items.empty())throw YouTubeError(YouTubeErrorKind::NotFound,"YouTube video is not available");
        if(items.size()!=1 || String(items.front(),"id")!=id)throw std::runtime_error("Mismatched video response");
        const auto& item=items.front();VideoInfo result;result.videoId=String(item,"id");
        if(auto snippet=item.Find("snippet")){result.channelId=String(*snippet,"channelId");result.title=String(*snippet,"title");}
        if(auto live=item.Find("liveStreamingDetails"))result.activeLiveChatId=String(*live,"activeLiveChatId");
        return result;
    }catch(const YouTubeError&){throw;}catch(...){throw YouTubeError(YouTubeErrorKind::InvalidResponse,"Invalid YouTube video response");}
}
ChatPoll YouTubeClient::Poll(bool discardMessages) {
    if(chatId_.empty())throw YouTubeError(YouTubeErrorKind::InvalidRequest,"BeginChat must be called before polling");
    if(Generation()!=chatGeneration_)throw YouTubeError(YouTubeErrorKind::Cancelled,"Chat session was cancelled");
    ChatPoll result;result.generation=chatGeneration_;result.nextPollMillis=nextPoll_;
    if(clock_()<nextPoll_){result.deferred=true;return result;}
    try {
        std::string path="/youtube/v3/liveChat/messages?part=id%2Csnippet%2CauthorDetails&liveChatId="+UrlEncode(chatId_)+"&maxResults=2000";
        if(!pageToken_.empty())path+="&pageToken="+UrlEncode(pageToken_);
        path+="&fields=nextPageToken%2CpollingIntervalMillis%2CofflineAt%2Citems(id%2Csnippet(type%2CliveChatId%2CauthorChannelId%2CpublishedAt%2CtextMessageDetails(messageText))%2CauthorDetails(channelId%2CdisplayName%2CisChatOwner%2CisChatModerator%2CisChatSponsor%2CisVerified))";
        auto response=Request(std::move(path),chatGeneration_);
        auto document=Json::Parse(response.body);
        auto token=String(document,"nextPageToken");if(token.size()>8192 || (token.empty() && String(document,"offlineAt").empty()))throw std::runtime_error("Invalid page token");
        const auto* intervalValue=document.Find("pollingIntervalMillis");auto interval=intervalValue?intervalValue->Integer():5000;
        if(interval<0 || interval>86400000)throw std::runtime_error("Invalid polling interval");
        interval=std::max<int64_t>(1000,interval);
        const auto& items=document.At("items").Items();if(items.size()>2000)throw std::runtime_error("Too many chat messages");
        std::vector<ChatMessage> parsed;parsed.reserve(items.size());
        for(const auto& item:items) {
            ChatMessage message;message.id=String(item,"id");Identifier(message.id,"message ID");
            const auto& snippet=item.At("snippet");message.type=String(snippet,"type");message.liveChatId=String(snippet,"liveChatId");
            if(message.liveChatId!=chatId_)throw std::runtime_error("Mismatched chat identity");
            message.publishedAt=String(snippet,"publishedAt");
            if(auto text=snippet.Find("textMessageDetails"))message.text=String(*text,"messageText");
            if(auto author=item.Find("authorDetails")) {
                message.author.channelId_=String(*author,"channelId");message.author.displayName_=String(*author,"displayName");
                message.author.liveChatId_=chatId_;
                const auto snippetAuthor=String(snippet,"authorChannelId");
                if(!snippetAuthor.empty() && snippetAuthor!=message.author.channelId_)throw std::runtime_error("Mismatched author identity");
                message.author.owner_=Boolean(*author,"isChatOwner");message.author.moderator_=Boolean(*author,"isChatModerator");
                message.author.member_=Boolean(*author,"isChatSponsor");message.author.verified_=Boolean(*author,"isVerified");
                message.author.fromApi_=!message.author.channelId_.empty();
            }
            parsed.push_back(std::move(message));
        }
        if(Generation()!=chatGeneration_)throw YouTubeError(YouTubeErrorKind::Cancelled,"Discarded stale parsed response");
        result.initialHistory=first_;result.offlineAt=String(document,"offlineAt");
        // Parse and validate the entire page before committing token/dedup state.
        for(auto& message:parsed)if(seen_.insert(message.id).second) {
            seenOrder_.push_back(message.id);
            if(!discardMessages && !(first_&&discardHistory_))result.messages.push_back(std::move(message));
            // Bounded recent-message dedup. The command router owns its durable execution ledger.
            if(seenOrder_.size()>10000){seen_.erase(seenOrder_.front());seenOrder_.pop_front();}
        }
        pageToken_=std::move(token);first_=false;failures_=0;nextPoll_=clock_()+interval;result.nextPollMillis=nextPoll_;return result;
    }catch(const YouTubeError& error) {
        if(Retryable(error.kind)) {
            failures_=std::min<unsigned>(failures_+1,6);
            nextPoll_=clock_()+std::max<int64_t>(error.retryAfterMillis,std::min<int64_t>(60000,1000LL<<failures_));
        }
        else if(error.kind!=YouTubeErrorKind::Cancelled)Cancel();
        throw;
    }catch(...) {nextPoll_=clock_()+5000;throw YouTubeError(YouTubeErrorKind::InvalidResponse,"Invalid YouTube chat response");}
}
}
