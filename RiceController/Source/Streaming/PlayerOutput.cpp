#include "PlayerOutput.h"
#include "../../Shared/PlayerMediaChannel.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <optional>
extern "C" {
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}
namespace RiceController::Streaming {
struct PlayerOutput::Impl {
    struct Change {
        std::string runtime;
        std::shared_ptr<const RgbaImage> image;
        std::stop_token cancelled;
        std::promise<void> complete;
    };
    OutputProfile profile;std::string initialRuntime,url;std::filesystem::path local;
    mutable std::mutex mutex;std::condition_variable wake;
    std::deque<std::shared_ptr<Change>> changes;
    std::shared_ptr<std::atomic_bool> stopping=std::make_shared<std::atomic_bool>(false);
    std::promise<void> started;bool startedTaken=false,finished=false;std::string error;
    std::thread worker;
    Impl(OutputProfile value,std::string runtime,std::filesystem::path file,std::string destination)
        :profile(std::move(value)),initialRuntime(std::move(runtime)),url(std::move(destination)),local(std::move(file)) {
        ValidateProfile(profile);
        if(profile.streamDelaySeconds)throw std::invalid_argument("Player output delay buffer is not connected yet");
        worker=std::thread([this]{Run();});
    }
    ~Impl(){Stop();}
    void Stop(){stopping->store(true);wake.notify_all();if(worker.joinable())worker.join();}
    std::future<void> Queue(std::shared_ptr<Change> change){
        auto future=change->complete.get_future();std::lock_guard lock(mutex);
        if(finished||stopping->load())throw std::runtime_error(error.empty()?"Output is stopped":error);
        if(changes.size()>=8)throw std::runtime_error("Output source-switch queue is full");
        changes.push_back(std::move(change));wake.notify_all();return future;
    }
    void Run() noexcept {
        bool didStart=false;
        std::shared_ptr<Change> applying;
        std::int64_t appliedPts=-1;
        SwsContext* scaler=nullptr;SwrContext* resampler=nullptr;
        struct Free {SwsContext*& s;SwrContext*& r;~Free(){sws_freeContext(s);swr_free(&r);}} cleanup{scaler,resampler};
        try {
            auto encoder=OpenHardwareVideoEncoder(profile);
            auto output=url.empty()?OpenLocalOutput(local,encoder->Format()):OpenRtmpsOutput(url,encoder->Format(),stopping);
            auto source=std::make_unique<PlayerMediaChannel>(initialRuntime,false);
            source->ReadAudio(true);
            PlayerMediaChannel::Frame frame;
            std::shared_ptr<const RgbaImage> waiting;
            std::vector<std::uint8_t> scaled(static_cast<std::size_t>(profile.width)*profile.height*4);
            std::vector<float> audioFifo;
            std::int64_t videoPts=-1,audioPts=0;
            const int block=output->AudioFrameSamples();
            std::vector<float> audioBlock(static_cast<std::size_t>(block)*2);
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&resampler,&stereo,AV_SAMPLE_FMT_FLT,profile.audioSampleRate,
                &stereo,AV_SAMPLE_FMT_FLT,48000,0,nullptr)<0 || swr_init(resampler)<0)
                throw std::runtime_error("Cannot initialize game audio resampler");
            using Clock=std::chrono::steady_clock;
            std::optional<Clock::time_point> origin;
            const auto writePackets=[&](const std::vector<EncodedVideoPacket>& packets){
                for(const auto& packet:packets){
                    output->WriteVideo(packet);
                    if(!didStart){started.set_value();didStart=true;}
                    if(applying && packet.pts>=appliedPts){
                        if(applying->cancelled.stop_requested())applying->complete.set_exception(std::make_exception_ptr(std::runtime_error("Source switch cancelled")));
                        else applying->complete.set_value();
                        applying.reset();
                    }
                }
            };
            while(!stopping->load()){
                if(!applying){
                    std::shared_ptr<Change> next;
                    {std::lock_guard lock(mutex);if(!changes.empty()){next=std::move(changes.front());changes.pop_front();}}
                    if(next){
                        if(next->cancelled.stop_requested())next->complete.set_exception(std::make_exception_ptr(std::runtime_error("Source switch cancelled")));
                        else try{
                            if(next->image)waiting=next->image;
                            else {
                                auto candidate=std::make_unique<PlayerMediaChannel>(next->runtime,false);
                                PlayerMediaChannel::Frame first;
                                if(!candidate->ReadVideo(first)){
                                    std::lock_guard lock(mutex);changes.push_front(next);
                                }else{candidate->ReadAudio(true);source=std::move(candidate);frame=std::move(first);waiting.reset();applying=next;}
                            }
                            if(next->image)applying=next;
                            if(applying){audioFifo.clear();swr_close(resampler);if(swr_init(resampler)<0)throw std::runtime_error("Reset game audio resampler");appliedPts=videoPts+1;}
                        }catch(...){next->complete.set_exception(std::current_exception());applying.reset();}
                    }
                }
                if(!waiting)source->ReadVideo(frame);
                if(!waiting && frame.rgba.empty()){
                    std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(2));continue;
                }
                if(!origin)origin=Clock::now();
                const auto elapsed=std::chrono::duration<double>(Clock::now()-*origin).count();
                const auto targetVideo=static_cast<std::int64_t>(elapsed*profile.fps);
                if(targetVideo>videoPts){
                    const auto* pixels=waiting?waiting->pixels.data():frame.rgba.data();
                    const int width=waiting?waiting->width:frame.width,height=waiting?waiting->height:frame.height;
                    scaler=sws_getCachedContext(scaler,width,height,AV_PIX_FMT_RGBA,profile.width,profile.height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
                    if(!scaler)throw std::runtime_error("Cannot scale Player frame");
                    const std::uint8_t* src[]={pixels,nullptr,nullptr,nullptr};const int strides[]={width*4,0,0,0};
                    std::uint8_t* dst[]={scaled.data(),nullptr,nullptr,nullptr};const int dstStrides[]={profile.width*4,0,0,0};
                    if(sws_scale(scaler,src,strides,0,height,dst,dstStrides)!=profile.height)throw std::runtime_error("Scale Player frame failed");
                    videoPts=targetVideo;writePackets(encoder->EncodeRgba(scaled,profile.width*4,videoPts));
                }
                if(waiting)source->ReadAudio(true);
                else {
                    auto input=source->ReadAudio();
                    if(!input.empty()){
                        const auto inputFrames=static_cast<int>(input.size()/2);
                        const int capacity=swr_get_out_samples(resampler,inputFrames);
                        if(capacity<0 || capacity>192000)throw std::runtime_error("Game audio resampling capacity exceeded");
                        std::vector<float> converted(static_cast<std::size_t>(capacity)*2);
                        const std::uint8_t* in[]={reinterpret_cast<const std::uint8_t*>(input.data())};
                        std::uint8_t* out[]={reinterpret_cast<std::uint8_t*>(converted.data())};
                        const int count=swr_convert(resampler,out,capacity,in,inputFrames);
                        if(count<0)throw std::runtime_error("Game audio resampling failed");
                        audioFifo.insert(audioFifo.end(),converted.begin(),converted.begin()+count*2);
                        const auto bound=static_cast<std::size_t>(profile.audioSampleRate/10)*2;
                        if(audioFifo.size()>bound)audioFifo.erase(audioFifo.begin(),audioFifo.end()-bound);
                    }
                }
                const auto targetAudio=static_cast<std::int64_t>(elapsed*profile.audioSampleRate);
                for(int budget=0;budget<8 && audioPts+block<=targetAudio;++budget){
                    std::fill(audioBlock.begin(),audioBlock.end(),0.f);
                    const auto count=std::min(audioBlock.size(),audioFifo.size());
                    std::copy_n(audioFifo.begin(),count,audioBlock.begin());audioFifo.erase(audioFifo.begin(),audioFifo.begin()+count);
                    output->WriteAudio(audioBlock,audioPts);audioPts+=block;
                }
                std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(2));
            }
            // Local verification files should be finalized. Cancelled network
            // operations are bounded by the output interrupt callback.
            if(url.empty()){writePackets(encoder->Drain());output->Finish();}
        }catch(const std::exception& failure){std::lock_guard lock(mutex);error=failure.what();}
        catch(...){std::lock_guard lock(mutex);error="Unknown media output failure";}
        const auto exception=std::make_exception_ptr(std::runtime_error(error.empty()?"Output stopped":error));
        if(!didStart)try{started.set_exception(exception);}catch(...){}
        if(applying)try{applying->complete.set_exception(exception);}catch(...){}
        std::lock_guard lock(mutex);finished=true;
        for(auto& change:changes)try{change->complete.set_exception(exception);}catch(...){}changes.clear();
    }
};
PlayerOutput::PlayerOutput(OutputProfile profile,std::string runtime,std::filesystem::path file)
    :impl_(std::make_unique<Impl>(std::move(profile),std::move(runtime),std::move(file),std::string{})){}
PlayerOutput::PlayerOutput(OutputProfile profile,std::string runtime,std::string url)
    :impl_(std::make_unique<Impl>(std::move(profile),std::move(runtime),std::filesystem::path{},std::move(url))){}
PlayerOutput::~PlayerOutput()=default;
std::future<void> PlayerOutput::Started(){std::lock_guard lock(impl_->mutex);if(impl_->startedTaken)throw std::logic_error("Started future already consumed");impl_->startedTaken=true;return impl_->started.get_future();}
std::future<void> PlayerOutput::ShowWaiting(std::shared_ptr<const RgbaImage> image,std::stop_token cancelled){
    if(!image || image->width<1 || image->height<1 || image->width>7680 || image->height>4320 || image->pixels.size()!=static_cast<std::size_t>(image->width)*image->height*4)
        throw std::invalid_argument("Invalid waiting frame");
    auto change=std::make_shared<Impl::Change>();change->image=std::move(image);change->cancelled=cancelled;return impl_->Queue(change);
}
std::future<void> PlayerOutput::SwitchSource(std::string runtime,std::stop_token cancelled){
    auto change=std::make_shared<Impl::Change>();change->runtime=std::move(runtime);change->cancelled=cancelled;return impl_->Queue(change);
}
void PlayerOutput::Stop(){impl_->Stop();}
std::string PlayerOutput::Error() const {std::lock_guard lock(impl_->mutex);return impl_->error;}
}
