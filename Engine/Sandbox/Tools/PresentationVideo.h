#pragma once
#include "Streaming/VideoEncoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

// Local presentation export only; the live streaming muxer remains unchanged.
namespace PresentationVideo {
inline void Require(int result,const char* operation){
    if(result>=0)return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(result,message,sizeof(message));
    throw std::runtime_error(std::string(operation)+": "+message);
}
inline std::string PathText(const std::filesystem::path& path){
    auto text=std::filesystem::absolute(path).u8string();return {text.begin(),text.end()};
}

class Mp4Writer {
    AVFormatContext* output=nullptr;
    AVStream* stream=nullptr;
    AVPacket* packet=nullptr;
    AVRational sourceTimeBase{};
    int64_t lastDts=AV_NOPTS_VALUE;
    bool finished=false;
    void Clear()noexcept{
        av_packet_free(&packet);
        if(output){if(output->pb)avio_closep(&output->pb);avformat_free_context(output);output=nullptr;}
    }
public:
    Mp4Writer(const std::filesystem::path& path,const RiceController::Streaming::EncodedVideoFormat& video){
        using namespace RiceController::Streaming;
        if(video.profile.codec!=VideoCodec::H264)throw std::invalid_argument("Presentation MP4 requires H.264");
        if(video.profile.width<=0||video.profile.height<=0||video.profile.fps<=0||
           video.extraData.empty()||video.extraData.size()>1024*1024)
            throw std::invalid_argument("Invalid presentation video format");
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        const auto target=PathText(path);
        try{
            Require(avformat_alloc_output_context2(&output,nullptr,"mp4",target.c_str()),"Create presentation MP4");
            if(!output)throw std::bad_alloc();
            stream=avformat_new_stream(output,nullptr);packet=av_packet_alloc();
            if(!stream||!packet)throw std::bad_alloc();
            sourceTimeBase={1,video.profile.fps};stream->time_base=sourceTimeBase;
            stream->avg_frame_rate={video.profile.fps,1};stream->r_frame_rate=stream->avg_frame_rate;
            auto* parameters=stream->codecpar;
            parameters->codec_type=AVMEDIA_TYPE_VIDEO;parameters->codec_id=AV_CODEC_ID_H264;
            parameters->width=video.profile.width;parameters->height=video.profile.height;
            parameters->format=AV_PIX_FMT_NV12;
            parameters->bit_rate=int64_t(video.profile.videoBitrateKbps)*1000;
            parameters->color_range=AVCOL_RANGE_MPEG;parameters->color_space=AVCOL_SPC_BT709;
            parameters->color_primaries=AVCOL_PRI_BT709;parameters->color_trc=AVCOL_TRC_BT709;
            parameters->extradata=static_cast<uint8_t*>(av_mallocz(video.extraData.size()+AV_INPUT_BUFFER_PADDING_SIZE));
            if(!parameters->extradata)throw std::bad_alloc();
            std::memcpy(parameters->extradata,video.extraData.data(),video.extraData.size());
            parameters->extradata_size=int(video.extraData.size());
            Require(avio_open(&output->pb,target.c_str(),AVIO_FLAG_WRITE),"Open presentation MP4");
            AVDictionary* options=nullptr;
            struct OptionsGuard{AVDictionary*& value;~OptionsGuard(){av_dict_free(&value);}} guard{options};
            av_dict_set(&options,"movflags","+faststart",0);
            Require(avformat_write_header(output,&options),"Write presentation MP4 header");
        }catch(...){Clear();throw;}
    }
    Mp4Writer(const Mp4Writer&)=delete;
    Mp4Writer& operator=(const Mp4Writer&)=delete;
    ~Mp4Writer(){Clear();}
    void Write(const RiceController::Streaming::EncodedVideoPacket& value){
        if(finished)throw std::logic_error("Presentation MP4 already finished");
        if(value.bytes.empty()||value.bytes.size()>64*1024*1024||value.pts<0||value.dts<0||
           (lastDts!=AV_NOPTS_VALUE&&value.dts<=lastDts))
            throw std::invalid_argument("Invalid presentation video packet");
        Require(av_new_packet(packet,int(value.bytes.size())),"Allocate presentation packet");
        std::memcpy(packet->data,value.bytes.data(),value.bytes.size());
        packet->pts=value.pts;packet->dts=value.dts;packet->duration=std::max<int64_t>(1,value.duration);
        packet->stream_index=stream->index;
        if(value.keyframe)packet->flags|=AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(packet,sourceTimeBase,stream->time_base);
        int result=av_interleaved_write_frame(output,packet);av_packet_unref(packet);
        Require(result,"Write presentation video packet");lastDts=value.dts;
    }
    void Finish(){
        if(finished)return;
        Require(av_write_trailer(output),"Finish presentation MP4");
        Require(avio_closep(&output->pb),"Close presentation MP4");finished=true;
    }
};

inline void SaveDecodedBmp(const std::filesystem::path& path,const AVFrame& frame){
    const int stride=frame.width*3;
    std::vector<uint8_t> bgr(size_t(stride)*frame.height);
    SwsContext* scaler=sws_getContext(frame.width,frame.height,static_cast<AVPixelFormat>(frame.format),
        frame.width,frame.height,AV_PIX_FMT_BGR24,SWS_BILINEAR,nullptr,nullptr,nullptr);
    if(!scaler)throw std::runtime_error("Create decoded preview converter");
    struct ScaleGuard{SwsContext* value;~ScaleGuard(){sws_freeContext(value);}} guard{scaler};
    const int* coefficients=sws_getCoefficients(SWS_CS_ITU709);
    Require(sws_setColorspaceDetails(scaler,coefficients,frame.color_range==AVCOL_RANGE_JPEG,coefficients,1,
        0,1<<16,1<<16),"Set decoded preview colors");
    uint8_t* destination[]={bgr.data(),nullptr,nullptr,nullptr};int strides[]={stride,0,0,0};
    if(sws_scale(scaler,frame.data,frame.linesize,0,frame.height,destination,strides)!=frame.height)
        throw std::runtime_error("Convert decoded preview");
    const uint32_t rowBytes=(uint32_t(stride)+3u)&~3u;
    std::ofstream out(path,std::ios::binary);
    auto byte=[&](uint32_t n){out.put(char(n));};
    auto word=[&](uint32_t n){byte(n);byte(n>>8);};
    auto dword=[&](uint32_t n){word(n);word(n>>16);};
    byte('B');byte('M');dword(54+rowBytes*frame.height);dword(0);dword(54);
    dword(40);dword(frame.width);dword(frame.height);word(1);word(24);dword(0);
    dword(rowBytes*frame.height);dword(0);dword(0);dword(0);dword(0);
    for(int y=frame.height-1;y>=0;--y){
        out.write(reinterpret_cast<const char*>(bgr.data()+size_t(y)*stride),stride);
        for(uint32_t padding=uint32_t(stride);padding<rowBytes;++padding)byte(0);
    }
    if(!out)throw std::runtime_error("Write decoded presentation preview");
}

struct Verification {int frames=0,width=0,height=0;double fps=0,durationSeconds=0;};
inline Verification Verify(const std::filesystem::path& path,int expectedWidth,int expectedHeight,
                           int expectedFps,int expectedFrames,bool saveFrames=true,const std::filesystem::path& previewDirectory={}){
    if(expectedWidth<=0||expectedHeight<=0||expectedFps<=0||expectedFrames<=0)
        throw std::invalid_argument("Invalid presentation verification expectations");
    AVFormatContext* input=nullptr;AVCodecContext* decoder=nullptr;AVPacket* packet=nullptr;AVFrame* frame=nullptr;
    struct Cleanup{
        AVFormatContext*& input;AVCodecContext*& decoder;AVPacket*& packet;AVFrame*& frame;
        ~Cleanup(){av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&decoder);avformat_close_input(&input);}
    } cleanup{input,decoder,packet,frame};
    const auto target=PathText(path);
    Require(avformat_open_input(&input,target.c_str(),nullptr,nullptr),"Read recorded MP4");
    Require(avformat_find_stream_info(input,nullptr),"Inspect recorded MP4");
    int index=av_find_best_stream(input,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);Require(index,"Find recorded video");
    auto* stream=input->streams[index];
    if(stream->codecpar->codec_id!=AV_CODEC_ID_H264||stream->codecpar->width!=expectedWidth||stream->codecpar->height!=expectedHeight)
        throw std::runtime_error("Recorded codec or dimensions mismatch");
    const double fps=av_q2d(av_guess_frame_rate(input,stream,nullptr));
    if(std::abs(fps-expectedFps)>.001)throw std::runtime_error("Recorded frame rate mismatch");
    const AVCodec* codec=avcodec_find_decoder(stream->codecpar->codec_id);
    if(!codec)throw std::runtime_error("Recorded video decoder unavailable");
    decoder=avcodec_alloc_context3(codec);packet=av_packet_alloc();frame=av_frame_alloc();
    if(!decoder||!packet||!frame)throw std::bad_alloc();
    Require(avcodec_parameters_to_context(decoder,stream->codecpar),"Load recorded video decoder parameters");
    Require(avcodec_open2(decoder,codec,nullptr),"Open recorded video decoder");
    Verification report{0,expectedWidth,expectedHeight,fps,0};
    const double tolerance=std::max(.0001,2*av_q2d(stream->time_base));
    const std::pair<double,const char*> previews[]={{0,"decoded-start.bmp"},{4,"decoded-motion.bmp"},
        {6.5,"decoded-cache.bmp"},{11,"decoded-returned.bmp"}};
    auto receive=[&]{
        for(;;){
            int result=avcodec_receive_frame(decoder,frame);
            if(result==AVERROR(EAGAIN)||result==AVERROR_EOF)return;
            Require(result,"Decode recorded frame");
            if(frame->width!=expectedWidth||frame->height!=expectedHeight||frame->best_effort_timestamp==AV_NOPTS_VALUE)
                throw std::runtime_error("Decoded presentation frame is invalid");
            double timestamp=frame->best_effort_timestamp*av_q2d(stream->time_base);
            if(std::abs(timestamp-double(report.frames)/expectedFps)>tolerance)
                throw std::runtime_error("Recorded frames have a gap or unexpected timestamp");
            if(saveFrames)for(auto [time,name]:previews)if(report.frames==int(std::llround(time*expectedFps)))
                SaveDecodedBmp((previewDirectory.empty()?path.parent_path():previewDirectory)/name,*frame);
            ++report.frames;av_frame_unref(frame);
        }
    };
    int result=0;
    while((result=av_read_frame(input,packet))>=0){
        if(packet->stream_index==index){Require(avcodec_send_packet(decoder,packet),"Submit recorded packet");receive();}
        av_packet_unref(packet);
    }
    if(result!=AVERROR_EOF)Require(result,"Read recorded packets");
    Require(avcodec_send_packet(decoder,nullptr),"Drain recorded decoder");receive();
    if(report.frames!=expectedFrames)throw std::runtime_error("Recorded frame count mismatch");
    report.durationSeconds=stream->duration==AV_NOPTS_VALUE?double(input->duration)/AV_TIME_BASE:
        stream->duration*av_q2d(stream->time_base);
    if(std::abs(report.durationSeconds-double(expectedFrames)/expectedFps)>std::max(tolerance,.001))
        throw std::runtime_error("Recorded duration mismatch");
    return report;
}
}
