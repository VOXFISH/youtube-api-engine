#pragma once
#include "Rice/Benchmark.h"
#include "Scene/Scene.h"
#include <fmod.hpp>
#include <fmod_errors.h>
#include <unordered_map>
#include <functional>
#include <span>
#include <cstring>
namespace Rice {
class AudioSystem {
    FMOD::System* system=nullptr;
    std::unordered_map<std::string,FMOD::Sound*> sounds;
    FMOD::DSP* capture=nullptr;
    std::function<void(std::span<const float>)> captureSink;
    static FMOD_RESULT F_CALL CaptureRead(FMOD_DSP_STATE* state,float* input,float* output,unsigned length,int channels,int* outputChannels){
        if(input && output && channels>0)std::memcpy(output,input,static_cast<std::size_t>(length)*channels*sizeof(float));
        *outputChannels=channels;
        void* opaque=nullptr;
        if(state->functions->getuserdata(state,&opaque)!=FMOD_OK || !opaque)return FMOD_OK;
        auto& audio=*static_cast<AudioSystem*>(opaque);
        if(input && channels==2 && audio.captureSink)try{audio.captureSink({input,static_cast<std::size_t>(length)*2});}catch(...){}
        return FMOD_OK;
    }
public:
    static void Check(FMOD_RESULT result){ RICE_BENCHMARK_FUNCTION(); if(result!=FMOD_OK)throw std::runtime_error(FMOD_ErrorString(result));}
    explicit AudioSystem(bool silent=false){ RICE_BENCHMARK_FUNCTION(); Check(FMOD::System_Create(&system));try{Check(system->setSoftwareFormat(48000,FMOD_SPEAKERMODE_STEREO,0));if(silent)Check(system->setOutput(FMOD_OUTPUTTYPE_NOSOUND));Check(system->init(128,FMOD_INIT_NORMAL,nullptr));}catch(...){system->release();system=nullptr;throw;}}
    AudioSystem(const AudioSystem&)=delete;
    ~AudioSystem(){ RICE_BENCHMARK_FUNCTION(); if(capture){FMOD::ChannelGroup* group=nullptr;if(system->getMasterChannelGroup(&group)==FMOD_OK)group->removeDSP(capture);capture->release();capture=nullptr;}for(auto& [key,sound]:sounds)sound->release();if(system)system->release();}
    void CaptureGameMix(std::function<void(std::span<const float>)> sink){
        if(capture)throw std::logic_error("Game audio capture is already installed");
        captureSink=std::move(sink);
        FMOD_DSP_DESCRIPTION description{};description.pluginsdkversion=FMOD_PLUGIN_SDK_VERSION;
        std::memcpy(description.name,"Rice game mix",14);description.numinputbuffers=1;description.numoutputbuffers=1;
        description.read=CaptureRead;description.userdata=this;
        Check(system->createDSP(&description,&capture));
        FMOD::ChannelGroup* group=nullptr;Check(system->getMasterChannelGroup(&group));Check(group->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL,capture));
    }
    FMOD::Channel* Play(const std::string& path,bool loop,float volume){
        RICE_BENCHMARK_FUNCTION();
        FMOD::Sound* sound=nullptr;auto it=sounds.find(path);if(it!=sounds.end())sound=it->second;
        else{Check(system->createSound(path.c_str(),FMOD_DEFAULT,nullptr,&sound));sounds.emplace(path,sound);}
        FMOD::Channel* channel=nullptr;Check(system->playSound(sound,nullptr,true,&channel));
        Check(channel->setMode(loop?FMOD_LOOP_NORMAL:FMOD_LOOP_OFF));Check(channel->setVolume(glm::clamp(volume,0.f,1.f)));Check(channel->setPaused(false));return channel;
    }
    void Update(){ RICE_BENCHMARK_FUNCTION(); Check(system->update());}
    void PauseAll(bool paused){ RICE_BENCHMARK_FUNCTION(); FMOD::ChannelGroup* group=nullptr;Check(system->getMasterChannelGroup(&group));Check(group->setPaused(paused));}
    void StopAll(){ RICE_BENCHMARK_FUNCTION(); FMOD::ChannelGroup* group=nullptr;Check(system->getMasterChannelGroup(&group));Check(group->stop());}
};
struct AudioSource:Component {
    std::string clip;bool loop=false,playOnStart=false;float volume=1;
    FMOD::Channel* channel=nullptr;bool attempted=false;
    ~AudioSource() override{ RICE_BENCHMARK_FUNCTION(); Stop();}
    void Play(AudioSystem& audio,const std::string& path){ RICE_BENCHMARK_FUNCTION(); Stop();channel=audio.Play(path,loop,volume);}
    void Stop(){ RICE_BENCHMARK_FUNCTION(); if(channel)channel->stop();channel=nullptr;}
    void Pause(bool paused){ RICE_BENCHMARK_FUNCTION(); if(channel)AudioSystem::Check(channel->setPaused(paused));}
    bool IsPlaying() const{ RICE_BENCHMARK_FUNCTION(); bool playing=false;if(channel)channel->isPlaying(&playing);return playing;}
    void Sync(){ RICE_BENCHMARK_FUNCTION(); if(channel){auto r=channel->setVolume(glm::clamp(volume,0.f,1.f));if(r==FMOD_ERR_INVALID_HANDLE||r==FMOD_ERR_CHANNEL_STOLEN)channel=nullptr;else {AudioSystem::Check(r);AudioSystem::Check(channel->setMode(loop?FMOD_LOOP_NORMAL:FMOD_LOOP_OFF));}}}
    void OnDisable() override{ RICE_BENCHMARK_FUNCTION(); Stop();}
    void OnDestroy() override{ RICE_BENCHMARK_FUNCTION(); Stop();}
    void OnPoolAcquire() override{ RICE_BENCHMARK_FUNCTION(); Stop();attempted=false;}
};
}
