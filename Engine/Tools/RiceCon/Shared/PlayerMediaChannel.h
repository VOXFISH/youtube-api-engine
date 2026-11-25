#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace RiceController {
// Local, per-run transport. Video is a latest-frame mailbox; the capture side
// owns its bounded PBO queue. Audio has an independent lock and one-second ring.
// Neither publisher waits for a slow Controller consumer.
class PlayerMediaChannel {
    static constexpr int MaxWidth=3840,MaxHeight=2160,AudioFrames=48000;
    struct Shared {
        std::uint32_t magic,version,width,height;
        std::uint64_t frameSequence;
        std::int64_t frameQpc;
        std::uint8_t video[static_cast<std::size_t>(MaxWidth)*MaxHeight*4];
        std::uint64_t audioFramesWritten;
        float audio[AudioFrames*2];
    };
    HANDLE mapping_=nullptr,videoMutex_=nullptr,audioMutex_=nullptr;
    Shared* shared_=nullptr;bool writer_=false;
    std::uint64_t seenFrame_=0,audioCursor_=0;
    struct Lock {
        HANDLE handle;bool acquired=false;
        explicit Lock(HANDLE value):handle(value){
            const auto status=WaitForSingleObject(handle,0);
            if(status==WAIT_ABANDONED){ReleaseMutex(handle);throw std::runtime_error("Player media publisher terminated during a write");}
            if(status==WAIT_FAILED)throw std::runtime_error("Cannot lock Player media channel");
            acquired=status==WAIT_OBJECT_0;
        }
        ~Lock(){if(acquired)ReleaseMutex(handle);}
    };
    void Close() noexcept {
        if(shared_)UnmapViewOfFile(shared_);
        if(audioMutex_)CloseHandle(audioMutex_);if(videoMutex_)CloseHandle(videoMutex_);if(mapping_)CloseHandle(mapping_);
    }
    static std::wstring Name(const std::string& runId){
        if(runId.empty()||runId.size()>80||!std::all_of(runId.begin(),runId.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}))
            throw std::invalid_argument("Invalid media runtime ID");
        return L"Local\\RicePlayerMedia-"+std::wstring(runId.begin(),runId.end());
    }
public:
    struct Frame {int width=0,height=0;std::int64_t qpc=0;std::uint64_t sequence=0;std::vector<std::uint8_t> rgba;};
    explicit PlayerMediaChannel(const std::string& runId,bool writer):writer_(writer){
        const auto name=Name(runId);
        try{
            if(writer){
                mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(sizeof(Shared)),name.c_str());
                if(mapping_ && GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Media runtime already exists");
                videoMutex_=CreateMutexW(nullptr,FALSE,(name+L"-video").c_str());
                audioMutex_=CreateMutexW(nullptr,FALSE,(name+L"-audio").c_str());
            }else{
                mapping_=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
                videoMutex_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(name+L"-video").c_str());
                audioMutex_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(name+L"-audio").c_str());
            }
            if(!mapping_||!videoMutex_||!audioMutex_)throw std::runtime_error("Player media channel unavailable");
            shared_=static_cast<Shared*>(MapViewOfFile(mapping_,writer?FILE_MAP_ALL_ACCESS:FILE_MAP_READ,0,0,sizeof(Shared)));
            if(!shared_)throw std::runtime_error("Cannot map Player media channel");
            if(writer){shared_->version=1;shared_->magic=0x524d4544;}
            else if(shared_->magic!=0x524d4544||shared_->version!=1)throw std::runtime_error("Unsupported Player media channel");
        }catch(...){Close();throw;}
    }
    ~PlayerMediaChannel(){Close();}
    PlayerMediaChannel(const PlayerMediaChannel&)=delete;
    bool PublishVideo(std::span<const std::uint8_t> rgba,int width,int height,bool bottomUp=false){
        if(!writer_||width<1||height<1||width>MaxWidth||height>MaxHeight||rgba.size()<static_cast<std::size_t>(width)*height*4)
            throw std::invalid_argument("Invalid Player video frame");
        Lock lock(videoMutex_);if(!lock.acquired)return false;
        const auto row=static_cast<std::size_t>(width)*4;
        for(int y=0;y<height;++y)std::memcpy(shared_->video+y*row,rgba.data()+(bottomUp?height-y-1:y)*row,row);
        shared_->width=width;shared_->height=height;LARGE_INTEGER now;QueryPerformanceCounter(&now);shared_->frameQpc=now.QuadPart;++shared_->frameSequence;return true;
    }
    bool ReadVideo(Frame& frame){
        Lock lock(videoMutex_);if(!lock.acquired||shared_->frameSequence==seenFrame_)return false;
        if(!shared_->width||!shared_->height||shared_->width>MaxWidth||shared_->height>MaxHeight)throw std::runtime_error("Invalid shared video dimensions");
        frame.width=static_cast<int>(shared_->width);frame.height=static_cast<int>(shared_->height);
        frame.sequence=shared_->frameSequence;frame.qpc=shared_->frameQpc;
        frame.rgba.assign(shared_->video,shared_->video+static_cast<std::size_t>(frame.width)*frame.height*4);
        seenFrame_=frame.sequence;return true;
    }
    bool PublishAudio(std::span<const float> stereo){
        if(!writer_||stereo.size()%2||stereo.size()>AudioFrames*2)throw std::invalid_argument("Invalid Player audio block");
        Lock lock(audioMutex_);if(!lock.acquired)return false;
        const auto frames=stereo.size()/2;
        for(std::size_t i=0;i<frames;++i){const auto index=(shared_->audioFramesWritten+i)%AudioFrames;
            shared_->audio[index*2]=stereo[i*2];shared_->audio[index*2+1]=stereo[i*2+1];}
        shared_->audioFramesWritten+=frames;return true;
    }
    std::vector<float> ReadAudio(bool discardHistory=false){
        Lock lock(audioMutex_);if(!lock.acquired)return {};
        const auto end=shared_->audioFramesWritten;
        if(discardHistory){audioCursor_=end;return {};}
        if(end<audioCursor_)throw std::runtime_error("Player audio clock moved backwards");
        if(end-audioCursor_>AudioFrames)audioCursor_=end-AudioFrames;
        std::vector<float> result(static_cast<std::size_t>(end-audioCursor_)*2);
        for(std::size_t i=0;i<result.size()/2;++i){const auto index=(audioCursor_+i)%AudioFrames;
            result[i*2]=shared_->audio[index*2];result[i*2+1]=shared_->audio[index*2+1];}
        audioCursor_=end;return result;
    }
};
}
