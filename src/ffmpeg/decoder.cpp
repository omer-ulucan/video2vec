#include "video2vec/ffmpeg/decoder.hpp"

#include <cstring>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace video2vec::ffmpeg {

class Decoder::Impl {
public:
    const AVCodec* codec_ = nullptr;
    AVCodecContext* ctx_ = nullptr;
};

Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
Decoder::~Decoder() {
    if (impl_->ctx_) avcodec_free_context(&impl_->ctx_);
}
Decoder::Decoder(Decoder&& other) noexcept : impl_(std::move(other.impl_)) {}
Decoder& Decoder::operator=(Decoder&& other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

int Decoder::initialize(const AVCodecParameters* codec_params) {
    if (!codec_params) return AVERROR_INVALIDDATA;
    impl_->codec_ = avcodec_find_decoder(codec_params->codec_id);
    if (!impl_->codec_) return AVERROR_DECODER_NOT_FOUND;
    impl_->ctx_ = avcodec_alloc_context3(impl_->codec_);
    if (!impl_->ctx_) return AVERROR(ENOMEM);
    int ret = avcodec_parameters_to_context(impl_->ctx_, codec_params);
    if (ret < 0) { avcodec_free_context(&impl_->ctx_); return ret; }
    ret = avcodec_open2(impl_->ctx_, impl_->codec_, nullptr);
    if (ret < 0) { avcodec_free_context(&impl_->ctx_); return ret; }
    return 0;
}

int Decoder::send_packet(const AVPacket* packet) {
    if (!impl_->ctx_) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(impl_->ctx_, packet);
}

int Decoder::receive_frame(AVFrame* frame) {
    if (!impl_->ctx_) return AVERROR_INVALIDDATA;
    return avcodec_receive_frame(impl_->ctx_, frame);
}

void Decoder::flush() {
    if (impl_->ctx_) avcodec_flush_buffers(impl_->ctx_);
}

bool Decoder::is_initialized() const { return impl_->ctx_ != nullptr; }
const char* Decoder::codec_name() const { return impl_->codec_ ? impl_->codec_->name : "none"; }

class AudioDecoder::Impl {
public:
    const AVCodec* codec_ = nullptr;
    AVCodecContext* ctx_ = nullptr;
    int target_sample_rate_ = 16000;
    SwrContext* swr_ = nullptr;
};

AudioDecoder::AudioDecoder() : impl_(std::make_unique<Impl>()) {}
AudioDecoder::~AudioDecoder() {
    if (impl_->swr_) swr_free(&impl_->swr_);
    if (impl_->ctx_) avcodec_free_context(&impl_->ctx_);
}
AudioDecoder::AudioDecoder(AudioDecoder&& other) noexcept : impl_(std::move(other.impl_)) {}
AudioDecoder& AudioDecoder::operator=(AudioDecoder&& other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

int AudioDecoder::initialize(const AVCodecParameters* codec_params, int target_sample_rate) {
    if (!codec_params) return AVERROR_INVALIDDATA;
    impl_->target_sample_rate_ = target_sample_rate;
    impl_->codec_ = avcodec_find_decoder(codec_params->codec_id);
    if (!impl_->codec_) return AVERROR_DECODER_NOT_FOUND;
    impl_->ctx_ = avcodec_alloc_context3(impl_->codec_);
    if (!impl_->ctx_) return AVERROR(ENOMEM);
    int ret = avcodec_parameters_to_context(impl_->ctx_, codec_params);
    if (ret < 0) { avcodec_free_context(&impl_->ctx_); return ret; }
    ret = avcodec_open2(impl_->ctx_, impl_->codec_, nullptr);
    if (ret < 0) { avcodec_free_context(&impl_->ctx_); return ret; }

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 1);
    ret = swr_alloc_set_opts2(&impl_->swr_,
        &out_layout, AV_SAMPLE_FMT_FLT, target_sample_rate,
        &impl_->ctx_->ch_layout, impl_->ctx_->sample_fmt, impl_->ctx_->sample_rate,
        0, nullptr);
    if (ret < 0) { avcodec_free_context(&impl_->ctx_); return ret; }
    ret = swr_init(impl_->swr_);
    if (ret < 0) { swr_free(&impl_->swr_); avcodec_free_context(&impl_->ctx_); return ret; }
    return 0;
}

int AudioDecoder::send_packet(const AVPacket* packet) {
    if (!impl_->ctx_) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(impl_->ctx_, packet);
}

int AudioDecoder::receive_frame(AVFrame* frame) {
    if (!impl_->ctx_) return AVERROR_INVALIDDATA;
    return avcodec_receive_frame(impl_->ctx_, frame);
}

void AudioDecoder::flush() {
    if (impl_->ctx_) avcodec_flush_buffers(impl_->ctx_);
    if (impl_->swr_) swr_init(impl_->swr_);
}

bool AudioDecoder::is_initialized() const { return impl_->ctx_ != nullptr; }
int AudioDecoder::target_sample_rate() const { return impl_->target_sample_rate_; }

std::vector<uint8_t> frame_to_rgb(const AVFrame* frame, int& out_width, int& out_height) {
    if (!frame) { out_width = 0; out_height = 0; return {}; }
    out_width = frame->width; out_height = frame->height;
    int rgb_linesize = out_width * 3;
    std::vector<uint8_t> rgb_data(rgb_linesize * out_height);
    struct SwsContext* sws = sws_getContext(
        out_width, out_height, static_cast<AVPixelFormat>(frame->format),
        out_width, out_height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return {};
    const uint8_t* src_data[4] = {frame->data[0], frame->data[1], frame->data[2], frame->data[3]};
    int src_linesize[4] = {frame->linesize[0], frame->linesize[1], frame->linesize[2], frame->linesize[3]};
    uint8_t* dst_data[1] = {rgb_data.data()};
    int dst_linesize[1] = {rgb_linesize};
    sws_scale(sws, src_data, src_linesize, 0, out_height, dst_data, dst_linesize);
    sws_freeContext(sws);
    return rgb_data;
}

class AudioResampler::Impl {
public:
    SwrContext* swr_ = nullptr;
    int out_sample_rate_ = 16000;
    int out_channels_ = 1;
};

AudioResampler::AudioResampler() : impl_(std::make_unique<Impl>()) {}
AudioResampler::~AudioResampler() { if (impl_->swr_) swr_free(&impl_->swr_); }

bool AudioResampler::initialize(int in_sample_rate, int in_channels, AVSampleFormat in_fmt,
                                 int out_sample_rate, int out_channels, AVSampleFormat out_fmt) {
    if (impl_->swr_) swr_free(&impl_->swr_);
    AVChannelLayout in_layout, out_layout;
    av_channel_layout_default(&in_layout, in_channels);
    av_channel_layout_default(&out_layout, out_channels);
    int ret = swr_alloc_set_opts2(&impl_->swr_,
        &out_layout, out_fmt, out_sample_rate,
        &in_layout, in_fmt, in_sample_rate, 0, nullptr);
    if (ret < 0) return false;
    ret = swr_init(impl_->swr_);
    if (ret < 0) { swr_free(&impl_->swr_); return false; }
    impl_->out_sample_rate_ = out_sample_rate;
    impl_->out_channels_ = out_channels;
    return true;
}

std::vector<uint8_t> AudioResampler::resample(const AVFrame* frame, int& out_samples) {
    if (!impl_->swr_ || !frame) { out_samples = 0; return {}; }
    int max_samples = swr_get_out_samples(impl_->swr_, frame->nb_samples);
    std::vector<float> buffer(max_samples * impl_->out_channels_);
    uint8_t* out_ptr = reinterpret_cast<uint8_t*>(buffer.data());
    const uint8_t** in_data = const_cast<const uint8_t**>(frame->data);
    uint8_t** out_data = &out_ptr;
    int ret = swr_convert(impl_->swr_, out_data, max_samples, in_data, frame->nb_samples);
    if (ret < 0) { out_samples = 0; return {}; }
    out_samples = ret;
    size_t bytes = ret * impl_->out_channels_ * sizeof(float);
    std::vector<uint8_t> result(bytes);
    std::memcpy(result.data(), buffer.data(), bytes);
    return result;
}

void AudioResampler::flush(std::vector<uint8_t>& output, int& out_samples) {
    if (!impl_->swr_) { out_samples = 0; return; }
    int max_samples = swr_get_out_samples(impl_->swr_, 0);
    if (max_samples <= 0) { out_samples = 0; return; }
    std::vector<float> buffer(max_samples * impl_->out_channels_);
    uint8_t* out_ptr = reinterpret_cast<uint8_t*>(buffer.data());
    uint8_t** out_data = &out_ptr;
    int ret = swr_convert(impl_->swr_, out_data, max_samples, nullptr, 0);
    if (ret < 0) { out_samples = 0; return; }
    out_samples = ret;
    size_t bytes = ret * impl_->out_channels_ * sizeof(float);
    size_t old_size = output.size();
    output.resize(old_size + bytes);
    std::memcpy(output.data() + old_size, buffer.data(), bytes);
}

} // namespace video2vec::ffmpeg
