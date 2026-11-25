#include "SlotChatSession.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace RiceController::Streaming {
namespace {
std::int64_t NowMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::int64_t PublishedMillis(const std::string& text){
    if(text.size()<20 || text[4]!='-' || text[7]!='-' || text[10]!='T' || text[13]!=':' || text[16]!=':' || text.back()!='Z')return INT64_MIN;
    auto number=[&](std::size_t start,std::size_t count){int value=0;for(auto i=start;i<start+count;++i){if(text[i]<'0'||text[i]>'9')return -1;value=value*10+text[i]-'0';}return value;};
    const int y=number(0,4),m=number(5,2),d=number(8,2),h=number(11,2),minute=number(14,2),second=number(17,2);
    const std::chrono::year_month_day date{std::chrono::year{y},std::chrono::month{static_cast<unsigned>(m)},std::chrono::day{static_cast<unsigned>(d)}};
    if(!date.ok()||h<0||h>23||minute<0||minute>59||second<0||second>59)return INT64_MIN;
    int ms=0;
    if(text[19]=='.'){
        if(text.size()<22)return INT64_MIN;
        int multiplier=100;
        for(std::size_t i=20;i+1<text.size();++i){if(text[i]<'0'||text[i]>'9')return INT64_MIN;if(multiplier){ms+=(text[i]-'0')*multiplier;multiplier/=10;}}
    }else if(text.size()!=20)return INT64_MIN;
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::sys_days(date).time_since_epoch()).count()+
        (h*3600+minute*60+second)*1000LL+ms;
}
}
struct SlotChatSession::Impl {
    struct Waiter{std::promise<void> promise;std::stop_token cancelled;};
    std::unique_ptr<YouTubeClient> client;std::string chat;Sink sink;
    mutable std::mutex mutex;std::condition_variable wake;std::thread worker;
    bool stopping=false,healthy=false,finished=false,muted=false;
    std::uint64_t epoch=0;
    std::int64_t cutoff=INT64_MIN;
    std::string error;
    std::vector<std::unique_ptr<Waiter>> waiters;
    Impl(std::unique_ptr<YouTubeClient> c,std::string id,Sink callback):client(std::move(c)),chat(std::move(id)),sink(std::move(callback)){
        if(!client||chat.empty()||!sink)throw std::invalid_argument("Chat session requires client, identity and queued sink");
        worker=std::thread([this]{Run();});
    }
    ~Impl(){ {std::lock_guard lock(mutex);stopping=true;}client->Cancel();wake.notify_all();if(worker.joinable())worker.join(); }
    void CompleteWaiters(){
        for(auto it=waiters.begin();it!=waiters.end();){
            auto& waiter=**it;
            if(waiter.cancelled.stop_requested()){waiter.promise.set_exception(std::make_exception_ptr(std::runtime_error("Chat preparation cancelled")));it=waiters.erase(it);}
            else if(healthy){waiter.promise.set_value();it=waiters.erase(it);}
            else if(finished){waiter.promise.set_exception(std::make_exception_ptr(std::runtime_error(error.empty()?"Chat session stopped":error)));it=waiters.erase(it);}
            else ++it;
        }
    }
    void Run() noexcept {
        try {
            client->BeginChat(chat,true);
            for(;;){
                std::uint64_t requestEpoch;bool discard;
                {std::lock_guard lock(mutex);CompleteWaiters();if(stopping)break;requestEpoch=epoch;discard=muted;}
                try {
                    auto result=client->Poll(discard);
                    std::lock_guard lock(mutex);
                    if(!result.deferred){
                        if(!result.offlineAt.empty())throw std::runtime_error("YouTube live chat ended");
                        healthy=true;error.clear();CompleteWaiters();
                        if(!muted && requestEpoch==epoch)for(auto& message:result.messages)
                            if(message.IsTextCommandCandidate()&&PublishedMillis(message.publishedAt)>cutoff)sink(std::move(message),epoch);
                    }
                }catch(const YouTubeError& failure){
                    std::lock_guard lock(mutex);healthy=false;error=failure.what();
                    if(failure.kind!=YouTubeErrorKind::Network && failure.kind!=YouTubeErrorKind::Timeout &&
                       failure.kind!=YouTubeErrorKind::Server && failure.kind!=YouTubeErrorKind::RateLimited)throw;
                }
                std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopping;});
            }
        }catch(const std::exception& failure){std::lock_guard lock(mutex);error=failure.what();}
        catch(...){std::lock_guard lock(mutex);error="Unknown chat worker error";}
        std::lock_guard lock(mutex);healthy=false;finished=true;CompleteWaiters();
    }
};
SlotChatSession::SlotChatSession(std::unique_ptr<YouTubeClient> client,std::string chat,Sink sink)
    :impl_(std::make_unique<Impl>(std::move(client),std::move(chat),std::move(sink))){}
SlotChatSession::~SlotChatSession()=default;
std::future<void> SlotChatSession::Ready(std::stop_token token){
    auto waiter=std::make_unique<Impl::Waiter>();waiter->cancelled=token;auto result=waiter->promise.get_future();
    std::lock_guard lock(impl_->mutex);
    if(impl_->waiters.size()>=32)throw std::runtime_error("Too many chat readiness requests");
    impl_->waiters.push_back(std::move(waiter));impl_->CompleteWaiters();impl_->wake.notify_all();return result;
}
void SlotChatSession::SetMuted(bool value){
    std::lock_guard lock(impl_->mutex);
    if(impl_->muted==value)return;
    impl_->muted=value;++impl_->epoch;if(!value)impl_->cutoff=NowMillis();
}
std::string SlotChatSession::Error()const{std::lock_guard lock(impl_->mutex);return impl_->error;}
}
