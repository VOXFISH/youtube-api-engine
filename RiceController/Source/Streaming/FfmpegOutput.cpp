#include "EncodedOutput.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}
namespace RiceController::Streaming {
namespace {
void Require(int code, const char* operation) {
    // Deliberately omit libavformat's URL-bearing diagnostics from UI errors.
    if (code < 0) throw std::runtime_error(std::string(operation) + " (FFmpeg " + std::to_string(code) + ")");
}
class FfmpegOutput final : public EncodedOutput {
    AVFormatContext* output_ = nullptr;
    AVCodecContext* audio_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVStream* videoStream_ = nullptr;
    AVStream* audioStream_ = nullptr;
    AVRational videoTimeBase_{};
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::chrono::steady_clock::time_point deadline_;
    bool finished_ = false;
    std::int64_t lastVideoDts_ = AV_NOPTS_VALUE, lastAudioPts_ = -1;
    void Deadline() { deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10); }
    static int Interrupt(void* opaque) {
        const auto& self = *static_cast<FfmpegOutput*>(opaque);
        return (self.cancelled_ && self.cancelled_->load()) || std::chrono::steady_clock::now() >= self.deadline_;
    }
    void Clear() noexcept {
        av_packet_free(&packet_); av_frame_free(&frame_); avcodec_free_context(&audio_);
        if (output_) { if (output_->pb) { Deadline(); avio_closep(&output_->pb); } avformat_free_context(output_); }
    }
    void WritePacket(AVRational sourceTimeBase, AVStream* stream) {
        av_packet_rescale_ts(packet_, sourceTimeBase, stream->time_base);
        packet_->stream_index = stream->index;
        Deadline();
        Require(av_interleaved_write_frame(output_, packet_), "Write output packet");
    }
    void ReceiveAudio() {
        for (;;) {
            int result = avcodec_receive_packet(audio_, packet_);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
            Require(result, "Receive AAC packet");
            WritePacket(audio_->time_base, audioStream_);
        }
    }
public:
    FfmpegOutput(const std::string& target, const EncodedVideoFormat& video,
                 std::shared_ptr<std::atomic_bool> cancelled, bool network)
        : cancelled_(std::move(cancelled)) {
        ValidateProfile(video.profile);
        // FFmpeg's default logging can print ingest credentials in URLs. Rice
        // surfaces only operation names/error codes through its own diagnostics.
        static std::once_flag logging;
        std::call_once(logging, [] { av_log_set_level(AV_LOG_QUIET); });
        try {
            Require(avformat_alloc_output_context2(&output_, nullptr, "flv", nullptr), "Create FLV muxer");
            if (!output_) throw std::bad_alloc();
            output_->interrupt_callback = {Interrupt, this}; Deadline();
            output_->max_interleave_delta = 1000000;
            videoStream_ = avformat_new_stream(output_, nullptr);
            audioStream_ = avformat_new_stream(output_, nullptr);
            if (!videoStream_ || !audioStream_) throw std::bad_alloc();
            videoTimeBase_ = AVRational{1, video.profile.fps};
            videoStream_->time_base = videoTimeBase_;
            auto* params = videoStream_->codecpar;
            params->codec_type = AVMEDIA_TYPE_VIDEO;
            params->codec_id = video.profile.codec == VideoCodec::H264 ? AV_CODEC_ID_H264 :
                video.profile.codec == VideoCodec::HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_AV1;
            params->width = video.profile.width; params->height = video.profile.height;
            params->format = AV_PIX_FMT_NV12;
            params->color_range = AVCOL_RANGE_MPEG; params->color_space = AVCOL_SPC_BT709;
            params->color_primaries = AVCOL_PRI_BT709; params->color_trc = AVCOL_TRC_BT709;
            if (video.extraData.empty() || video.extraData.size() > 1024 * 1024)
                throw std::invalid_argument("Missing or excessive video codec header");
            params->extradata = static_cast<std::uint8_t*>(av_mallocz(video.extraData.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            if (!params->extradata) throw std::bad_alloc();
            std::memcpy(params->extradata, video.extraData.data(), video.extraData.size());
            params->extradata_size = static_cast<int>(video.extraData.size());
            const AVCodec* aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (!aac) throw std::runtime_error("AAC encoder unavailable");
            audio_ = avcodec_alloc_context3(aac); frame_ = av_frame_alloc(); packet_ = av_packet_alloc();
            if (!audio_ || !frame_ || !packet_) throw std::bad_alloc();
            audio_->sample_rate = video.profile.audioSampleRate;
            audio_->sample_fmt = AV_SAMPLE_FMT_FLTP;
            audio_->bit_rate = video.profile.audioBitrateKbps * 1000;
            audio_->time_base = AVRational{1, audio_->sample_rate};
            av_channel_layout_default(&audio_->ch_layout, 2);
            audio_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            Require(avcodec_open2(audio_, aac, nullptr), "Initialize AAC encoder");
            Require(avcodec_parameters_from_context(audioStream_->codecpar, audio_), "Copy AAC header");
            audioStream_->time_base = audio_->time_base;
            frame_->format = audio_->sample_fmt; frame_->sample_rate = audio_->sample_rate;
            frame_->nb_samples = audio_->frame_size;
            Require(av_channel_layout_copy(&frame_->ch_layout, &audio_->ch_layout), "Set stereo layout");
            Require(av_frame_get_buffer(frame_, 0), "Allocate audio frame");
            AVDictionary* options = nullptr;
            struct Dictionary { AVDictionary*& value; ~Dictionary() { av_dict_free(&value); } } guard{options};
            if (network) {
                av_dict_set(&options, "tls_verify", "1", 0);
                av_dict_set(&options, "rw_timeout", "10000000", 0);
            }
            Require(avio_open2(&output_->pb, target.c_str(), AVIO_FLAG_WRITE, &output_->interrupt_callback, &options), "Open output destination");
            av_dict_free(&options);
            av_dict_set(&options, "flvflags", "no_duration_filesize", 0);
            Deadline(); Require(avformat_write_header(output_, &options), "Write output header");
        } catch (...) { Clear(); throw; }
    }
    ~FfmpegOutput() override { Clear(); }
    int AudioFrameSamples() const noexcept override { return audio_->frame_size; }
    void WriteVideo(const EncodedVideoPacket& value) override {
        if (finished_) throw std::logic_error("Output already finished");
        if (value.bytes.empty() || value.bytes.size() > 64 * 1024 * 1024 ||
            value.pts < 0 || value.dts < 0 || (lastVideoDts_ != AV_NOPTS_VALUE && value.dts <= lastVideoDts_))
            throw std::invalid_argument("Invalid encoded video packet");
        Require(av_new_packet(packet_, static_cast<int>(value.bytes.size())), "Allocate output packet");
        std::memcpy(packet_->data, value.bytes.data(), value.bytes.size());
        packet_->pts = value.pts; packet_->dts = value.dts; packet_->duration = value.duration;
        if (value.keyframe) packet_->flags |= AV_PKT_FLAG_KEY;
        WritePacket(videoTimeBase_, videoStream_); lastVideoDts_ = value.dts;
    }
    void WriteAudio(std::span<const float> stereo, std::int64_t samplePts) override {
        if (finished_) throw std::logic_error("Output already finished");
        if (stereo.size() != static_cast<std::size_t>(audio_->frame_size) * 2 || samplePts < 0 ||
            (lastAudioPts_ >= 0 && samplePts < lastAudioPts_ + audio_->frame_size))
            throw std::invalid_argument("Invalid PCM block or audio timestamp");
        for (const float sample : stereo)
            if (!std::isfinite(sample)) throw std::invalid_argument("PCM samples must be finite");
        Require(av_frame_make_writable(frame_), "Make audio frame writable");
        for (int i = 0; i < audio_->frame_size; ++i) {
            reinterpret_cast<float*>(frame_->data[0])[i] = stereo[2 * i];
            reinterpret_cast<float*>(frame_->data[1])[i] = stereo[2 * i + 1];
        }
        frame_->pts = samplePts;
        Require(avcodec_send_frame(audio_, frame_), "Submit AAC frame");
        lastAudioPts_ = samplePts; ReceiveAudio();
    }
    void Finish() override {
        if (finished_) return;
        Require(avcodec_send_frame(audio_, nullptr), "Drain AAC encoder"); ReceiveAudio();
        Deadline(); Require(av_write_trailer(output_), "Finish output"); finished_ = true;
    }
};
}
std::unique_ptr<EncodedOutput> OpenLocalOutput(const std::filesystem::path& path, const EncodedVideoFormat& video) {
    const auto text = std::filesystem::absolute(path).u8string();
    return std::make_unique<FfmpegOutput>(std::string(text.begin(), text.end()), video, nullptr, false);
}
std::unique_ptr<EncodedOutput> OpenRtmpsOutput(const std::string& url, const EncodedVideoFormat& video,
                                            std::shared_ptr<std::atomic_bool> cancelled) {
    if (!url.starts_with("rtmps://") || url.size() > 8192 || url.find_first_of("\r\n\t") != std::string::npos ||
        url.find('\0') != std::string::npos || !cancelled)
        throw std::invalid_argument("A valid RTMPS URL and cancellation token are required");
    if (cancelled->load()) throw std::runtime_error("Output start cancelled");
    return std::make_unique<FfmpegOutput>(url, video, std::move(cancelled), true);
}
}
