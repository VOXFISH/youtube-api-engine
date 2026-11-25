#include "VideoEncoder.h"
#include <limits>
#include <stdexcept>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace RiceController::Streaming {
namespace {
void Require(int result, const char* operation) {
    if (result >= 0) return;
    char error[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, error, sizeof(error));
    throw std::runtime_error(std::string(operation) + ": " + error);
}
class FfmpegVideoEncoder final : public VideoEncoder {
    AVCodecContext* context_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* scaler_ = nullptr;
    bool drained_ = false;
    std::int64_t lastPts_ = -1;
    OutputProfile profile_;
    void Clear() noexcept {
        sws_freeContext(scaler_);
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&context_);
    }
    std::vector<EncodedVideoPacket> Receive() {
        std::vector<EncodedVideoPacket> result;
        for (;;) {
            const int status = avcodec_receive_packet(context_, packet_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) break;
            Require(status, "Receive hardware video packet");
            EncodedVideoPacket copy;
            copy.bytes.assign(packet_->data, packet_->data + packet_->size);
            copy.pts = packet_->pts; copy.dts = packet_->dts;
            copy.duration = packet_->duration;
            copy.keyframe = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
            av_packet_unref(packet_);
            result.push_back(std::move(copy));
        }
        return result;
    }
public:
    explicit FfmpegVideoEncoder(const OutputProfile& profile) : profile_(profile) {
        ValidateProfile(profile);
        const char* name = profile.codec == VideoCodec::H264 ? "h264_nvenc" :
                           profile.codec == VideoCodec::HEVC ? "hevc_nvenc" : "av1_nvenc";
        if (profile.encoder != "auto" && profile.encoder != name)
            throw std::runtime_error("Only the selected codec's NVENC hardware encoder is supported; CPU fallback is disabled");
        const AVCodec* codec = avcodec_find_encoder_by_name(name);
        if (!codec) throw std::runtime_error(std::string("Hardware encoder unavailable: ") + name);
        try {
            context_ = avcodec_alloc_context3(codec);
            frame_ = av_frame_alloc(); packet_ = av_packet_alloc();
            if (!context_ || !frame_ || !packet_) throw std::bad_alloc();
            context_->width = profile.width; context_->height = profile.height;
            context_->time_base = AVRational{1, profile.fps};
            context_->framerate = AVRational{profile.fps, 1};
            context_->pix_fmt = AV_PIX_FMT_NV12;
            context_->bit_rate = static_cast<std::int64_t>(profile.videoBitrateKbps) * 1000;
            context_->rc_max_rate = context_->bit_rate;
            context_->rc_buffer_size = static_cast<int>(context_->bit_rate);
            context_->gop_size = profile.fps * profile.keyframeSeconds;
            context_->max_b_frames = 0;
            context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            context_->color_range = AVCOL_RANGE_MPEG;
            context_->colorspace = AVCOL_SPC_BT709;
            context_->color_primaries = AVCOL_PRI_BT709;
            context_->color_trc = AVCOL_TRC_BT709;
            Require(av_opt_set(context_->priv_data, "preset", "p4", 0), "Set NVENC preset");
            Require(av_opt_set(context_->priv_data, "tune", "ll", 0), "Set NVENC tune");
            Require(av_opt_set(context_->priv_data, "rc", "cbr", 0), "Set NVENC rate control");
            Require(avcodec_open2(context_, codec, nullptr), "Initialize hardware encoder (new slot rejected; no CPU fallback)");
            frame_->format = context_->pix_fmt;
            frame_->width = context_->width; frame_->height = context_->height;
            frame_->color_range = context_->color_range;
            frame_->colorspace = context_->colorspace;
            frame_->color_primaries = context_->color_primaries;
            frame_->color_trc = context_->color_trc;
            Require(av_frame_get_buffer(frame_, 32), "Allocate video frame");
            scaler_ = sws_getContext(profile.width, profile.height, AV_PIX_FMT_RGBA,
                profile.width, profile.height, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler_) throw std::runtime_error("Create RGBA to NV12 converter");
            const int* coefficients = sws_getCoefficients(SWS_CS_ITU709);
            Require(sws_setColorspaceDetails(scaler_, coefficients, 1, coefficients, 0,
                0, 1 << 16, 1 << 16), "Set BT.709 conversion");
        } catch (...) { Clear(); throw; }
    }
    ~FfmpegVideoEncoder() override { Clear(); }
    EncodedVideoFormat Format() const override {
        EncodedVideoFormat result{profile_, {}};
        if (context_->extradata_size > 0)
            result.extraData.assign(context_->extradata, context_->extradata + context_->extradata_size);
        return result;
    }
    std::vector<EncodedVideoPacket> EncodeRgba(std::span<const std::uint8_t> pixels,
                                              int stride, std::int64_t pts) override {
        if (drained_) throw std::logic_error("Encoder has been drained");
        if (pts < 0 || pts <= lastPts_) throw std::invalid_argument("Video timestamps must increase");
        if (stride < context_->width * 4 ||
            pixels.size() < static_cast<std::size_t>(stride) * (context_->height - 1) + context_->width * 4)
            throw std::invalid_argument("Incomplete RGBA frame");
        Require(av_frame_make_writable(frame_), "Make video frame writable");
        const std::uint8_t* source[] = {pixels.data(), nullptr, nullptr, nullptr};
        int strides[] = {stride, 0, 0, 0};
        if (sws_scale(scaler_, source, strides, 0, context_->height, frame_->data, frame_->linesize) != context_->height)
            throw std::runtime_error("Convert RGBA video frame");
        frame_->pts = pts;
        Require(avcodec_send_frame(context_, frame_), "Submit hardware video frame");
        lastPts_ = pts;
        return Receive();
    }
    std::vector<EncodedVideoPacket> Drain() override {
        if (drained_) return {};
        Require(avcodec_send_frame(context_, nullptr), "Drain hardware encoder");
        drained_ = true;
        return Receive();
    }
};
}
std::unique_ptr<VideoEncoder> OpenHardwareVideoEncoder(const OutputProfile& profile) {
    return std::make_unique<FfmpegVideoEncoder>(profile);
}
}
