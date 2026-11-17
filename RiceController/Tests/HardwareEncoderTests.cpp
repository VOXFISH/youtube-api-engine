#include "Streaming/VideoEncoder.h"
#include "Streaming/EncodedOutput.h"
#include "Streaming/EncoderSessions.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
using namespace RiceController::Streaming;
int main(int argc, char** argv) {
    try {
        if (std::string(avcodec_license()).find("LGPL") == std::string::npos)
            throw std::runtime_error("Expected LGPL FFmpeg DLL configuration");
        for (const auto codec : {VideoCodec::H264, VideoCodec::HEVC, VideoCodec::AV1}) {
            OutputProfile p; p.codec = codec;
            auto encoder = OpenHardwareVideoEncoder(p);
            const auto format = encoder->Format();
            const AVCodecID id = codec == VideoCodec::H264 ? AV_CODEC_ID_H264 :
                                 codec == VideoCodec::HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_AV1;
            const AVCodec* decoder = codec == VideoCodec::AV1 ? avcodec_find_decoder_by_name("av1_cuvid") : avcodec_find_decoder(id);
            AVCodecContext* context = avcodec_alloc_context3(decoder);
            AVFrame* frame = av_frame_alloc();
            AVPacket* packet = av_packet_alloc();
            struct Cleanup {
                AVCodecContext*& c; AVFrame*& f; AVPacket*& p;
                ~Cleanup() { avcodec_free_context(&c); av_frame_free(&f); av_packet_free(&p); }
            } cleanup{context, frame, packet};
            if (!context || !frame || !packet) throw std::bad_alloc();
            if (!format.extraData.empty()) {
                context->extradata = static_cast<uint8_t*>(av_mallocz(format.extraData.size() + AV_INPUT_BUFFER_PADDING_SIZE));
                if (!context->extradata) throw std::bad_alloc();
                memcpy(context->extradata, format.extraData.data(), format.extraData.size());
                context->extradata_size = static_cast<int>(format.extraData.size());
            }
            if (avcodec_open2(context, decoder, nullptr) < 0) throw std::runtime_error("Open test decoder");
            const std::filesystem::path directory = argc > 1 ? argv[1] : "TestData/HardwareEncoder";
            std::filesystem::create_directories(directory);
            const auto path = directory / (std::string(CodecName(codec)) + ".flv");
            auto output = OpenLocalOutput(path, format);
            std::vector<float> silence(static_cast<std::size_t>(output->AudioFrameSamples()) * 2);
            int decoded = 0, encoded = 0;
            auto receive = [&] {
                int status;
                while ((status = avcodec_receive_frame(context, frame)) >= 0) {
                    if (frame->width != 1920 || frame->height != 1080) throw std::runtime_error("Decoded size mismatch");
                    ++decoded; av_frame_unref(frame);
                }
                if (status != AVERROR(EAGAIN) && status != AVERROR_EOF) throw std::runtime_error("Decode packet");
            };
            auto consume = [&](const std::vector<EncodedVideoPacket>& packets) {
                for (const auto& value : packets) {
                    output->WriteVideo(value);
                    if (av_new_packet(packet, static_cast<int>(value.bytes.size())) < 0) throw std::bad_alloc();
                    memcpy(packet->data, value.bytes.data(), value.bytes.size());
                    packet->pts = value.pts; packet->dts = value.dts;
                    if (avcodec_send_packet(context, packet) < 0) throw std::runtime_error("Submit test packet");
                    av_packet_unref(packet); ++encoded; receive();
                }
            };
            std::vector<std::uint8_t> rgba(1920 * 1080 * 4, 255);
            for (int i = 0; i < 12; ++i) {
                rgba[0] = static_cast<std::uint8_t>(i * 10);
                consume(encoder->EncodeRgba(rgba, 1920 * 4, i));
                output->WriteAudio(silence, static_cast<std::int64_t>(i) * output->AudioFrameSamples());
            }
            consume(encoder->Drain());
            avcodec_send_packet(context, nullptr); receive();
            if (encoded != 12 || decoded != 12) throw std::runtime_error("Hardware frame roundtrip count mismatch");
            output->Finish(); output.reset();
            AVFormatContext* file = nullptr;
            struct FileGuard { AVFormatContext*& f; ~FileGuard() { avformat_close_input(&f); } } fileGuard{file};
            if (avformat_open_input(&file, path.string().c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(file, nullptr) < 0)
                throw std::runtime_error("Read local output");
            int videoIndex = av_find_best_stream(file, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            int audioIndex = av_find_best_stream(file, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (videoIndex < 0 || audioIndex < 0 || file->streams[audioIndex]->codecpar->codec_id != AV_CODEC_ID_AAC)
                throw std::runtime_error("Missing video/AAC stream in local output");
            int muxedVideo = 0, muxedAudio = 0;
            while (av_read_frame(file, packet) >= 0) {
                if (packet->stream_index == videoIndex) ++muxedVideo;
                if (packet->stream_index == audioIndex) ++muxedAudio;
                av_packet_unref(packet);
            }
            if (muxedVideo != 12 || muxedAudio < 12) throw std::runtime_error("Local output packet count mismatch");
            std::cout << CodecName(codec) << ": 12 1080p frames encoded/decoded and AAC multiplexed\n";
        }
        EncoderSessions sessions;
        std::vector<std::shared_ptr<VideoEncoder>> concurrent;
        for (int i = 0; i < 5; ++i)
            concurrent.push_back(sessions.Acquire("slot-" + std::to_string(i), "game-" + std::to_string(i), OutputProfile{}));
        std::vector<std::uint8_t> rgba(1920 * 1080 * 4, 128);
        for (const auto& encoder : concurrent) {
            const auto first = encoder->EncodeRgba(rgba, 1920 * 4, 0);
            const auto rest = encoder->Drain();
            if (first.size() + rest.size() != 1) throw std::runtime_error("Concurrent encoder produced no frame");
        }
        std::cout << "Five independent 1080p NVENC sessions opened and produced frames (not a sustained FPS benchmark)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
