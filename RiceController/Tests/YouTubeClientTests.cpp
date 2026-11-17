#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Streaming/YouTubeClient.h"
#include "Streaming/CredentialStore.h"
#include "Streaming/PermissionPolicy.h"
#include "../Shared/Json.h"
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace RiceController::Streaming;
using RiceController::Json;
namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void Reject(F fn,const char* message){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Check(rejected,message);}
template<class F>void Error(YouTubeErrorKind expected,F fn,const char* message){bool rejected=false;try{fn();}catch(const YouTubeError& e){rejected=e.kind==expected;}Check(rejected,message);}
Json Message(std::string id,std::string channel="UC-person",bool owner=false,bool mod=false,std::string chat="chat-one") {
    return Json::Object{{"id",std::move(id)},
        {"snippet",Json::Object{{"type","textMessageEvent"},{"liveChatId",std::move(chat)},{"authorChannelId",channel},
            {"publishedAt","2026-11-16T10:00:00Z"},{"textMessageDetails",Json::Object{{"messageText","!회피 좌 3칸"}}}}},
        {"authorDetails",Json::Object{{"channelId",std::move(channel)},{"displayName","@trusted-looking-name"},
            {"isChatOwner",owner},{"isChatModerator",mod},{"isChatSponsor",true},{"isVerified",false}}}};
}
HttpResponse Page(std::string token,int interval,Json::Array items) {
    return {200,Json(Json::Object{{"nextPageToken",std::move(token)},{"pollingIntervalMillis",interval},{"items",std::move(items)}}).Dump()};
}
HttpResponse Failure(int status,const char* reason,int64_t retry=0) {
    return {status,Json(Json::Object{{"error",Json::Object{{"message","Never show provider free text or FAKE-SECRET"},
        {"errors",Json::Array{Json::Object{{"reason",reason}}}}}}}).Dump(),retry};
}
struct FakeTransport:HttpTransport {
    std::deque<std::function<HttpResponse(const HttpRequest&)>> steps;
    std::vector<HttpRequest> requests;
    void Add(HttpResponse result){steps.push_back([result=std::move(result)](const HttpRequest&){return result;});}
    HttpResponse Get(const HttpRequest& request)override {
        requests.push_back(request);Check(!steps.empty(),"Unexpected real request path");auto fn=std::move(steps.front());steps.pop_front();return fn(request);
    }
};
struct Fixture {
    int64_t now=1000;
    ApiKeyPool pool{"test-project"};
    std::shared_ptr<FakeTransport> transport=std::make_shared<FakeTransport>();
    YouTubeClient client;
    Fixture():client(transport,[](std::string_view ref)->std::optional<std::string>{return std::string("FAKE-")+std::string(ref);},{&pool,{}},[this]{return now;}) {
        pool.Add({"a","test-project","a"});pool.Add({"b","test-project","b"});
    }
};
void VideoAndPolling() {
    Fixture f;
    f.transport->Add({200,R"({"items":[{"id":"video-one","snippet":{"channelId":"UC-owner","title":"방송"},"liveStreamingDetails":{"activeLiveChatId":"chat-one"}}]})"});
    auto video=f.client.ReadVideo("video-one");Check(video.activeLiveChatId=="chat-one" && video.channelId=="UC-owner","Video lookup returns live chat and owner");
    Check(f.transport->requests[0].timeoutMillis==15000,"Total HTTP deadline defaults to 15 seconds");
    Check(f.transport->requests[0].path.find("&key=FAKE-a")!=std::string::npos,"Public read uses explicit supplied API key");
    f.client.BeginChat("chat-one");
    f.transport->Add(Page("next/a?",5000,{Message("old")}));auto first=f.client.Poll();
    Check(first.initialHistory && first.messages.empty() && first.nextPollMillis==6000,"First chat history is drained without executing commands");
    auto count=f.transport->requests.size();f.now=5999;Check(f.client.Poll().deferred && f.transport->requests.size()==count,"Polling interval suppresses HTTP call");
    f.now=6000;f.transport->Add(Page("next-two",0,{Message("old"),Message("new","UC-mod",false,true),Message("new","UC-mod",false,true)}));
    auto second=f.client.Poll();Check(second.messages.size()==1 && second.messages[0].id=="new","Duplicate history and duplicate IDs in one page are excluded");
    Check(second.messages[0].text=="!회피 좌 3칸" && second.messages[0].author.IsModerator() && second.messages[0].author.IsMember(),"Typed author flags and command text decoded");
    Check(f.transport->requests.back().path.find("pageToken=next%2Fa%3F")!=std::string::npos,"Server cursor encoded verbatim for next request");
    Check(second.nextPollMillis==7000,"Zero server interval still respects local minimum");
    f.now=7000;f.transport->Add(Page("next-three",1000,{Message("paused")}));Check(f.client.Poll(true).messages.empty(),"Pause drains commands without dispatching");
    f.now=8000;f.transport->Add(Page("next-four",1000,{Message("paused"),Message("resumed")}));
    auto resumed=f.client.Poll();Check(resumed.messages.size()==1 && resumed.messages[0].id=="resumed","Paused command cannot replay after resume");
    Check(f.pool.Current().key->keyId=="a","Success and polling never rotate API keys");
}
void ErrorAndFailover() {
    Fixture stale;stale.client.BeginChat("chat-one",false);
    stale.transport->steps.push_back([&](const HttpRequest&) {
        stale.pool.ReportFailure("a",ApiKeyFailure::InvalidKey); // Another slot already rotated this shared pool.
        return Failure(400,"keyInvalid");
    });
    stale.transport->Add(Page("after-rotation",1000,{Message("survives")}));
    Check(stale.client.Poll().messages.size()==1 && stale.pool.Current().key->keyId=="b",
        "A late credential failure retries the current replacement selected by another slot");
    Fixture f;f.client.BeginChat("chat-one",false);
    f.transport->Add(Failure(400,"keyInvalid"));f.transport->Add(Page("t",1000,{Message("one")}));
    auto response=f.client.Poll();Check(response.messages.size()==1 && f.pool.Current().key->keyId=="b","Invalid key fails over within one quota group");
    Check(f.transport->requests.size()==2 && f.transport->requests[1].path.find("key=FAKE-b")!=std::string::npos,"Replacement actually used for retry");
    f.now=response.nextPollMillis;f.transport->Add(Failure(403,"quotaExceeded"));
    Error(YouTubeErrorKind::QuotaExceeded,[&]{f.client.Poll();},"Project quota exhaustion classified");
    Check(f.pool.QuotaExhausted(),"Quota exhaustion blocks all pool keys");
    auto requests=f.transport->requests.size();f.client.BeginChat("chat-one");
    Error(YouTubeErrorKind::QuotaExceeded,[&]{f.client.Poll();},"Starting a new session cannot bypass quota");
    Check(f.transport->requests.size()==requests,"No request issued to exhausted project");
    Fixture rate;rate.client.BeginChat("chat-one",false);rate.transport->Add(Failure(429,"rateLimitExceeded",20000));
    Error(YouTubeErrorKind::RateLimited,[&]{rate.client.Poll();},"Rate error classified");
    Check(rate.client.NextPollMillis()==21000 && rate.pool.Current().key->keyId=="a","Retry-After respects same credential and delayed polling");
    Check(rate.client.Poll().deferred,"Backoff does not send another HTTP request");
    Fixture denied;denied.client.BeginChat("chat-one");denied.transport->Add(Failure(403,"forbidden"));
    Error(YouTubeErrorKind::Forbidden,[&]{denied.client.Poll();},"Permissions cannot be fixed by rotating API keys");
    Check(denied.pool.Current().key->keyId=="a" && denied.transport->requests.size()==1,"Forbidden leaves credential pool untouched");
    Error(YouTubeErrorKind::Cancelled,[&]{denied.client.Poll();},"Terminal API errors halt polling until explicit restart");
}
void CancellationAndInvalidResponses() {
    WinHttpTransport real;HttpRequest guarded;guarded.path="https://untrusted.example/youtube/v3/videos";
    Error(YouTubeErrorKind::InvalidRequest,[&]{real.Get(guarded);},"Transport rejects arbitrary hosts before any network operation");
    guarded.path="/youtube/v3/videos?part=id&id=test";guarded.cancelled=[]{return true;};
    Error(YouTubeErrorKind::Cancelled,[&]{real.Get(guarded);},"Transport rejects cancelled generation before opening a session");
    Fixture cancelled;cancelled.client.BeginChat("chat-one",false);
    cancelled.transport->steps.push_back([&](const HttpRequest& request){cancelled.client.Cancel();Check(request.cancelled(),"Transport sees cancelled generation");return Page("stale",1000,{Message("never")});});
    Error(YouTubeErrorKind::Cancelled,[&]{cancelled.client.Poll();},"Response after cancellation is discarded");
    Check(cancelled.client.PageToken().empty(),"Stale response cannot advance cursor");
    cancelled.client.BeginChat("chat-one",false);cancelled.transport->Add(Page("fresh",1000,{Message("never")}));
    Check(cancelled.client.Poll().messages.size()==1,"New generation does not inherit stale dedup state");
    Fixture timeout;timeout.client.BeginChat("chat-one",false);
    timeout.transport->steps.push_back([&](const HttpRequest& request){Check(request.timeoutMillis==15000,"15 second bound passed to transport");timeout.now+=15001;return Page("late",1000,{Message("late")});});
    Error(YouTubeErrorKind::Timeout,[&]{timeout.client.Poll();},"Late response cannot escape total deadline");
    Check(timeout.client.PageToken().empty(),"Timeout cannot commit cursor");
    Fixture invalid;invalid.client.BeginChat("chat-one",false);invalid.transport->Add(Page("bad",1000,{Message("valid"),Message("bad","UC-x",false,false,"other-chat")}));
    Error(YouTubeErrorKind::InvalidResponse,[&]{invalid.client.Poll();},"Mixed chat page rejected atomically");
    Check(invalid.client.PageToken().empty(),"Partial malformed page does not advance cursor");
    invalid.now=invalid.client.NextPollMillis();invalid.transport->Add(Page("fixed",1000,{Message("valid")}));
    Check(invalid.client.Poll().messages.size()==1,"Rejected page does not poison dedup state");
    Fixture body;body.client.BeginChat("chat-one");body.transport->Add({200,std::string(4*1024*1024+1,'x')});
    Error(YouTubeErrorKind::InvalidResponse,[&]{body.client.Poll();},"Oversized response rejected even from injected transport");
    Fixture secretError;secretError.transport->steps.push_back([](const HttpRequest&)->HttpResponse{throw std::runtime_error("transport accidentally included FAKE-SECRET");});
    try{secretError.client.ReadVideo("test");throw std::runtime_error("Expected transport failure");}
    catch(const YouTubeError& error){Check(std::string(error.what()).find("FAKE-SECRET")==std::string::npos,"Injected transport exception cannot leak credential into UI logs");}
}
void OAuthAndPermissions() {
    auto transport=std::make_shared<FakeTransport>();int64_t now=0;
    YouTubeClient client(transport,[](std::string_view ref)->std::optional<std::string>{Check(ref=="token-reference","Explicit OAuth reference only");return "FAKE-ACCESS-TOKEN";},{nullptr,"token-reference"},[&]{return now;});
    client.BeginChat("chat-one",false);transport->Add(Page("t",1000,{Message("owner","UC-owner",true),Message("mod","UC-mod",false,true),Message("public","UC-person")}));
    auto messages=client.Poll().messages;Check(transport->requests[0].headers[0].second=="Bearer FAKE-ACCESS-TOKEN","OAuth uses supplied bearer token");
    Check(transport->requests[0].path.find("key=")==std::string::npos,"OAuth does not silently attach a separate API project key");
    PermissionPolicy policy("chat-one");policy.Register("default");policy.Register("play",{Permission::Everyone,{}});
    policy.Register("owner",{Permission::Owner,{}});policy.Register("mod",{Permission::OwnerOrModerator,{}});
    policy.Register("allow",{Permission::ChannelAllowlist,{"UC-person"}});
    Check(!policy.AllowsRemote("default",messages[0].author) && policy.AllowsLocalOperator("default"),"Unspecified command permissions default to local admin");
    Check(!policy.AllowsLocalOperator("unknown") && !policy.AllowsRemote("unknown",messages[0].author),"Unknown commands never execute");
    Check(policy.AllowsRemote("play",messages[2].author) && !policy.AllowsRemote("play",VerifiedAuthor{}),"Public command still requires API provenance");
    Check(policy.AllowsRemote("owner",messages[0].author) && !policy.AllowsRemote("owner",messages[1].author),"Owner authority is distinct from moderator");
    Check(policy.AllowsRemote("mod",messages[0].author) && policy.AllowsRemote("mod",messages[1].author) && !policy.AllowsRemote("mod",messages[2].author),"Owner/mod flags enforced");
    Check(policy.AllowsRemote("allow",messages[2].author) && !policy.AllowsRemote("allow",messages[0].author),"Allowlist compares stable channel ID instead of display name");
    Reject([&]{policy.Register("invalid",{Permission::ChannelAllowlist,{"@trusted-looking-name"}});},"Handles need resolution before entering channel allowlist");
    PermissionPolicy otherChat("chat-two");otherChat.Register("owner",{Permission::Owner,{}});
    Check(!otherChat.AllowsRemote("owner",messages[0].author),"Owner authority from one live chat cannot authorize another slot's chat");
}
void Credentials(const std::filesystem::path& base) {
    // Tests create only their own unique namespace and synthetic credentials.
    const auto root=std::filesystem::absolute(base)/("case-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    CredentialStore store(root);const std::string fake="FAKE-SECRET-ONLY-FOR-TESTS-2026";
    Check(!store.Get("synthetic"),"Missing reference does not access another credential store");
    store.Put("synthetic",fake);Check(store.Get("synthetic")==fake,"Current-user DPAPI roundtrip");
    std::ifstream input(root/"synthetic.dpapi",std::ios::binary);std::ostringstream raw;raw<<input.rdbuf();input.close();
    Check(raw.str().find(fake)==std::string::npos,"Secret is not stored in plaintext");
    store.Put("synthetic","FAKE-REPLACEMENT");Check(store.Get("synthetic")=="FAKE-REPLACEMENT","Credential replacement commits atomically");
    std::filesystem::copy_file(root/"synthetic.dpapi",root/"other.dpapi");Reject([&]{store.Get("other");},"Reference-bound DPAPI entropy prevents blob substitution");
    Reject([&]{store.Put("../escape",fake);},"Credential reference cannot escape namespace");
    Reject([&]{store.Get("..\\escape");},"Credential reads cannot escape namespace");
    Check(store.Erase("synthetic") && !store.Get("synthetic") && !store.Erase("synthetic"),"Credential deletion is isolated and idempotent");
    store.Erase("other");std::filesystem::remove(root); // Removes only this empty generated directory.
    auto redacted=Redact("url?key=FAKE-A&x=1 Authorization: Bearer FAKE-TOKEN\nrefresh_token=FAKE-R "+fake,{fake});
    Check(redacted.find("FAKE-")==std::string::npos && redacted.find("x=1")!=std::string::npos,"Log redaction removes credentials and preserves useful context");
}
}
int main(int argc,char** argv) {
    try {
        VideoAndPolling();ErrorAndFailover();CancellationAndInvalidResponses();OAuthAndPermissions();
        Credentials(argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/"RiceController-YouTubeClientTests");
        std::cout<<"YouTube API, credential and permission tests passed (mock HTTP, no account access)\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
