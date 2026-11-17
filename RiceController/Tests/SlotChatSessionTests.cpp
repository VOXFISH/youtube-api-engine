#include "Streaming/SlotChatSession.h"
#include "../Shared/Json.h"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
using namespace RiceController::Streaming;
using RiceController::Json;
namespace {
void Check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
struct Transport:HttpTransport {
    std::mutex mutex;std::condition_variable wake;
    unsigned requested=0,delivered=0;bool available=false;HttpResponse response;
    HttpResponse Get(const HttpRequest& request) override {
        std::unique_lock lock(mutex);++requested;wake.notify_all();
        while(!available){if(request.cancelled())throw YouTubeError(YouTubeErrorKind::Cancelled,"cancelled");wake.wait_for(lock,std::chrono::milliseconds(10));}
        available=false;++delivered;wake.notify_all();return response;
    }
    void Wait(unsigned n){std::unique_lock lock(mutex);Check(wake.wait_for(lock,std::chrono::seconds(5),[&]{return requested>=n;}),"poll did not start");}
    void Reply(std::string id,std::string published="2099-01-01T00:00:00Z"){
        Json message=Json::Object{{"id",id},{"snippet",Json::Object{{"type","textMessageEvent"},{"liveChatId","chat"},{"authorChannelId","UC-test"},{"publishedAt",published},{"textMessageDetails",Json::Object{{"messageText","!test"}}}}},{"authorDetails",Json::Object{{"channelId","UC-test"}}}};
        std::lock_guard lock(mutex);response={200,Json(Json::Object{{"nextPageToken",id},{"pollingIntervalMillis",1000},{"items",Json::Array{message}}}).Dump()};available=true;wake.notify_all();
    }
};
void Run(){
    auto transport=std::make_shared<Transport>();std::atomic<int64_t> clock=0;
    auto client=std::make_unique<YouTubeClient>(transport,[](std::string_view)->std::optional<std::string>{return "test-token";},YouTubeAuthentication{nullptr,"test-ref"},[&]{return clock.fetch_add(1000);});
    std::mutex mutex;std::condition_variable wake;std::vector<std::string> received;
    SlotChatSession session(std::move(client),"chat",[&](ChatMessage message,uint64_t){std::lock_guard lock(mutex);received.push_back(message.id);wake.notify_all();});
    auto ready=session.Ready({});transport->Wait(1);
    Check(ready.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout,"ready before API response");
    transport->Reply("history");Check(ready.wait_for(std::chrono::seconds(5))==std::future_status::ready,"readiness not acknowledged");ready.get();
    transport->Wait(2);session.SetMuted(true);transport->Reply("inflight-before-mute");
    transport->Wait(3);transport->Reply("during-wait");
    transport->Wait(4);session.SetMuted(false);transport->Reply("inflight-before-resume");
    transport->Wait(5);transport->Reply("late-old-message","2000-01-01T00:00:00Z");
    transport->Wait(6);transport->Reply("fresh");
    {std::unique_lock lock(mutex);Check(wake.wait_for(lock,std::chrono::seconds(5),[&]{return !received.empty();}),"fresh command missing");Check(received==std::vector<std::string>{"fresh"},"muted/history/inflight commands replayed");}
    std::stop_source cancelled;cancelled.request_stop();auto rejected=session.Ready(cancelled.get_token());bool threw=false;try{rejected.get();}catch(const std::exception&){threw=true;}Check(threw,"cancelled readiness accepted");
    // Destruction cancels a blocked transport request without waiting for a page.
    transport->Wait(7);
}
}
int main(){try{Run();std::cout<<"Slot chat gating passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
