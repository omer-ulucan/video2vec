#include "decoder_impl.hpp"
#include "ffmpeg_helpers.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace video2vec::ffmpeg {

namespace {

AVCodecID codec_id_from_name(const char* name) {
    if (!name) return AV_CODEC_ID_NONE;
    const AVCodec* codec = avcodec_find_decoder_by_name(name);
    if (codec) return codec->id;
    // Try common aliases
    if (std::strcmp(name, "h264") == 0) return AV_CODEC_ID_H264;
    if (std::strcmp(name, "hevc") == 0) return AV_CODEC_ID_HEVC;
    if (std::strcmp(name, "av1") == 0) return AV_CODEC_ID_AV1;
    if (std::strcmp(name, "vp9") == 0) return AV_CODEC_ID_VP9;
    if (std::strcmp(name, "aac") == 0) return AV_CODEC_ID_AAC;
    if (std::strcmp(name, "mp3") == 0) return AV_CODEC_ID_MP3;
    if (std::strcmp(name, "opus") == 0) return AV_CODEC_ID_OPUS;
    if (std::strcmp(name, "pcm_s16le") == 0) return AV_CODEC_ID_PCM_S16LE;
    if (std::strcmp(name, "pcm_f32le") == 0) return AV_CODEC_ID_PCM_F32LE;
    return AV_CODEC_ID_NONE;
}

// Allocates a context for `codec`, optionally copies `params` into it, and
// opens it. On failure *ctx is left null.
int open_codec_context(const AVCodec* codec, const AVCodecParameters* params, AVCodecContext** ctx) {
    *ctx = avcodec_alloc_context3(codec);
    if (!*ctx) return AVERROR(ENOMEM);
    if (params) {
        int ret = avcodec_parameters_to_context(*ctx, params);
        if (ret < 0) { avcodec_free_context(ctx); return ret; }
    }
    return 0;
}

int finish_open(const AVCodec* codec, AVCodecContext** ctx) {
    int ret = avcodec_open2(*ctx, codec, nullptr);
    if (ret < 0) avcodec_free_context(ctx);
    return ret;
}

} // namespace

// ------------------------------------------------------------------
// Decoder
// ------------------------------------------------------------------
class Decoder::Impl {
public:
    ~Impl() { reset(); }
    void reset() {
        if (ctx_) avcodec_free_context(&ctx_);
        codec_ = nullptr;
    }
    const AVCodec* codec_ = nullptr;
    AVCodecContext* ctx_ = nullptr;
};

Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
Decoder::~Decoder() = default;
Decoder::Decoder(Decoder&& other) noexcept = default;
Decoder& Decoder::operator=(Decoder&& other) noexcept = default;

int Decoder::initialize(const AVCodecParameters* codec_params) {
    if (!impl_ || !codec_params) return AVERROR_INVALIDDATA;
    impl_->reset();
    impl_->codec_ = avcodec_find_decoder(codec_params->codec_id);
    if (!impl_->codec_) return AVERROR_DECODER_NOT_FOUND;
    int ret = open_codec_context(impl_->codec_, codec_params, &impl_->ctx_);
    if (ret < 0) return ret;
    return finish_open(impl_->codec_, &impl_->ctx_);
}

int Decoder::initialize(const char* codec_id, int width, int height, int sample_rate, int channels) {
    if (!impl_) return AVERROR_INVALIDDATA;
    AVCodecID id = codec_id_from_name(codec_id);
    if (id == AV_CODEC_ID_NONE) return AVERROR_DECODER_NOT_FOUND;
    impl_->reset();
    impl_->codec_ = avcodec_find_decoder(id);
    if (!impl_->codec_) return AVERROR_DECODER_NOT_FOUND;
    int ret = open_codec_context(impl_->codec_, nullptr, &impl_->ctx_);
    if (ret < 0) return ret;
    impl_->ctx_->width = width;
    impl_->ctx_->height = height;
    impl_->ctx_->sample_rate = sample_rate;
    av_channel_layout_default(&impl_->ctx_->ch_layout, std::max(0, channels));
    return finish_open(impl_->codec_, &impl_->ctx_);
}

int Decoder::send_packet(const Packet& packet) {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    const AVPacket* pkt = native_packet(packet);
    if (!pkt) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(impl_->ctx_, pkt);
}

int Decoder::send_eof() {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(impl_->ctx_, nullptr);
}

int Decoder::receive_frame(Frame& frame) {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    AVFrame* fr = native_frame(frame);
    if (!fr) return AVERROR_INVALIDDATA;
    return avcodec_receive_frame(impl_->ctx_, fr);
}

void Decoder::flush() {
    if (impl_ && impl_->ctx_) avcodec_flush_buffers(impl_->ctx_);
}

bool Decoder::is_initialized() const { return impl_ && impl_->ctx_ != nullptr; }
const char* Decoder::codec_name() const { return (impl_ && impl_->codec_) ? impl_->codec_->name : "none"; }

// ------------------------------------------------------------------
// AudioDecoder
// ------------------------------------------------------------------
class AudioDecoder::Impl {
public:
    ~Impl() {
        reset();
        if (scratch_) av_frame_free(&scratch_);
    }

    void reset() {
        release_swr();
        if (ctx_) avcodec_free_context(&ctx_);
        codec_ = nullptr;
    }

    void release_swr() {
        if (swr_) swr_free(&swr_);
        av_channel_layout_uninit(&in_layout_);
        in_fmt_ = AV_SAMPLE_FMT_NONE;
        in_rate_ = 0;
    }

    // (Re)configures the resampler for the layout/format/rate of `f`. The
    // codec context's sample_fmt is not reliable before the first frame has
    // been decoded, so the resampler is always derived from real frames.
    int ensure_swr(const AVFrame* f) {
        const bool unchanged = swr_ && f->format == in_fmt_ && f->sample_rate == in_rate_ &&
                               av_channel_layout_compare(&f->ch_layout, &in_layout_) == 0;
        if (unchanged) return 0;
        release_swr();
        int ret = av_channel_layout_copy(&in_layout_, &f->ch_layout);
        if (ret < 0) return ret;
        AVChannelLayout out_layout;
        av_channel_layout_default(&out_layout, 1);
        ret = swr_alloc_set_opts2(&swr_,
            &out_layout, AV_SAMPLE_FMT_FLT, target_sample_rate_,
            &in_layout_, static_cast<AVSampleFormat>(f->format), f->sample_rate,
            0, nullptr);
        av_channel_layout_uninit(&out_layout);
        if (ret < 0) { release_swr(); return ret; }
        ret = swr_init(swr_);
        if (ret < 0) { release_swr(); return ret; }
        in_fmt_ = static_cast<AVSampleFormat>(f->format);
        in_rate_ = f->sample_rate;
        return 0;
    }

    // Appends up to `max_out` converted samples to `out`; returns samples written or AVERROR.
    int convert_into(std::vector<float>& out, int max_out, const uint8_t** in_data, int in_samples) {
        if (max_out <= 0) return 0;
        const size_t old_size = out.size();
        out.resize(old_size + static_cast<size_t>(max_out));
        uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out.data() + old_size);
        int n = swr_convert(swr_, &out_ptr, max_out, in_data, in_samples);
        if (n < 0) { out.resize(old_size); return n; }
        out.resize(old_size + static_cast<size_t>(n));
        return n;
    }

    const AVCodec* codec_ = nullptr;
    AVCodecContext* ctx_ = nullptr;
    int target_sample_rate_ = 16000;
    SwrContext* swr_ = nullptr;
    AVChannelLayout in_layout_{};
    AVSampleFormat in_fmt_ = AV_SAMPLE_FMT_NONE;
    int in_rate_ = 0;
    AVFrame* scratch_ = nullptr;
};

AudioDecoder::AudioDecoder() : impl_(std::make_unique<Impl>()) {}
AudioDecoder::~AudioDecoder() = default;
AudioDecoder::AudioDecoder(AudioDecoder&& other) noexcept = default;
AudioDecoder& AudioDecoder::operator=(AudioDecoder&& other) noexcept = default;

int AudioDecoder::initialize(const AVCodecParameters* codec_params, int target_sample_rate) {
    if (!impl_ || !codec_params || target_sample_rate <= 0) return AVERROR_INVALIDDATA;
    impl_->reset();
    impl_->target_sample_rate_ = target_sample_rate;
    impl_->codec_ = avcodec_find_decoder(codec_params->codec_id);
    if (!impl_->codec_) return AVERROR_DECODER_NOT_FOUND;
    int ret = open_codec_context(impl_->codec_, codec_params, &impl_->ctx_);
    if (ret < 0) return ret;
    return finish_open(impl_->codec_, &impl_->ctx_);
}

int AudioDecoder::initialize(const char* codec_id, int sample_rate, int channels, int target_sample_rate) {
    if (!impl_ || target_sample_rate <= 0) return AVERROR_INVALIDDATA;
    AVCodecID id = codec_id_from_name(codec_id);
    if (id == AV_CODEC_ID_NONE) return AVERROR_DECODER_NOT_FOUND;
    impl_->reset();
    impl_->target_sample_rate_ = target_sample_rate;
    impl_->codec_ = avcodec_find_decoder(id);
    if (!impl_->codec_) return AVERROR_DECODER_NOT_FOUND;
    int ret = open_codec_context(impl_->codec_, nullptr, &impl_->ctx_);
    if (ret < 0) return ret;
    impl_->ctx_->sample_rate = sample_rate;
    av_channel_layout_default(&impl_->ctx_->ch_layout, std::max(0, channels));
    return finish_open(impl_->codec_, &impl_->ctx_);
}

int AudioDecoder::send_packet(const Packet& packet) {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    const AVPacket* pkt = native_packet(packet);
    if (!pkt) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(impl_->ctx_, pkt);
}

int AudioDecoder::send_eof() {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(impl_->ctx_, nullptr);
}

int AudioDecoder::receive_frame(Frame& frame) {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    AVFrame* fr = native_frame(frame);
    if (!fr) return AVERROR_INVALIDDATA;
    return avcodec_receive_frame(impl_->ctx_, fr);
}

int AudioDecoder::receive_samples(std::vector<float>& out) {
    if (!impl_ || !impl_->ctx_) return AVERROR_INVALIDDATA;
    if (!impl_->scratch_) {
        impl_->scratch_ = av_frame_alloc();
        if (!impl_->scratch_) return AVERROR(ENOMEM);
    }
    AVFrame* f = impl_->scratch_;
    av_frame_unref(f);
    int ret = avcodec_receive_frame(impl_->ctx_, f);
    if (ret < 0) return ret;
    if (f->nb_samples <= 0) { av_frame_unref(f); return 0; }
    ret = impl_->ensure_swr(f);
    if (ret < 0) { av_frame_unref(f); return ret; }
    int max_out = swr_get_out_samples(impl_->swr_, f->nb_samples);
    if (max_out < 0) { av_frame_unref(f); return max_out; }
    int n = impl_->convert_into(out, max_out, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
    av_frame_unref(f);
    return n;
}

int AudioDecoder::drain_samples(std::vector<float>& out) {
    if (!impl_ || !impl_->swr_) return 0;
    int max_out = swr_get_out_samples(impl_->swr_, 0);
    if (max_out <= 0) return 0;
    return impl_->convert_into(out, max_out, nullptr, 0);
}

void AudioDecoder::flush() {
    if (!impl_) return;
    if (impl_->ctx_) avcodec_flush_buffers(impl_->ctx_);
    impl_->release_swr();  // re-created lazily from the next decoded frame
}

bool AudioDecoder::is_initialized() const { return impl_ && impl_->ctx_ != nullptr; }
int AudioDecoder::target_sample_rate() const { return impl_ ? impl_->target_sample_rate_ : 0; }

// ------------------------------------------------------------------
// frame_to_rgb
// ------------------------------------------------------------------
std::vector<uint8_t> frame_to_rgb(const Frame& frame, int& out_width, int& out_height) {
    out_width = 0;
    out_height = 0;
    const AVFrame* avf = native_frame(frame);
    if (!avf || avf->width <= 0 || avf->height <= 0 || !avf->data[0]) return {};
    const AVPixelFormat fmt = static_cast<AVPixelFormat>(avf->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    // Hardware surfaces carry opaque handles in data[0], not pixels.
    if (!desc || (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) return {};

    const size_t width = static_cast<size_t>(avf->width);
    const size_t height = static_cast<size_t>(avf->height);
    if (width > static_cast<size_t>(std::numeric_limits<int>::max()) / 3) return {};
    const int rgb_linesize = static_cast<int>(width * 3);
    if (height > std::numeric_limits<size_t>::max() / static_cast<size_t>(rgb_linesize)) return {};
    std::vector<uint8_t> rgb_data(static_cast<size_t>(rgb_linesize) * height);

    struct SwsContext* sws = sws_getContext(
        avf->width, avf->height, fmt,
        avf->width, avf->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return {};
    const uint8_t* src_data[4] = {avf->data[0], avf->data[1], avf->data[2], avf->data[3]};
    int src_linesize[4] = {avf->linesize[0], avf->linesize[1], avf->linesize[2], avf->linesize[3]};
    uint8_t* dst_data[1] = {rgb_data.data()};
    int dst_linesize[1] = {rgb_linesize};
    int rows = sws_scale(sws, src_data, src_linesize, 0, avf->height, dst_data, dst_linesize);
    sws_freeContext(sws);
    if (rows != avf->height) return {};
    out_width = avf->width;
    out_height = avf->height;
    return rgb_data;
}

// ------------------------------------------------------------------
// AudioResampler
// ------------------------------------------------------------------
class AudioResampler::Impl {
public:
    ~Impl() { if (swr_) swr_free(&swr_); }
    SwrContext* swr_ = nullptr;
    int out_sample_rate_ = 16000;
    int out_channels_ = 1;
    AVSampleFormat out_fmt_ = AV_SAMPLE_FMT_FLT;
    int out_bytes_per_sample_ = 4;

    // Converts up to `max_samples` frames into a fresh interleaved byte buffer.
    std::vector<uint8_t> convert(int max_samples, const uint8_t** in_data, int in_samples, int& out_samples) {
        out_samples = 0;
        if (max_samples <= 0) return {};
        const size_t frame_bytes = static_cast<size_t>(out_channels_) * static_cast<size_t>(out_bytes_per_sample_);
        std::vector<uint8_t> buffer(static_cast<size_t>(max_samples) * frame_bytes);
        uint8_t* out_ptr = buffer.data();
        int ret = swr_convert(swr_, &out_ptr, max_samples, in_data, in_samples);
        if (ret < 0) return {};
        buffer.resize(static_cast<size_t>(ret) * frame_bytes);
        out_samples = ret;
        return buffer;
    }
};

AudioResampler::AudioResampler() : impl_(std::make_unique<Impl>()) {}
AudioResampler::~AudioResampler() = default;
AudioResampler::AudioResampler(AudioResampler&& other) noexcept = default;
AudioResampler& AudioResampler::operator=(AudioResampler&& other) noexcept = default;

bool AudioResampler::initialize(int in_sample_rate, int in_channels, int in_fmt,
                                 int out_sample_rate, int out_channels, int out_fmt) {
    if (!impl_) return false;
    if (impl_->swr_) swr_free(&impl_->swr_);
    if (in_sample_rate <= 0 || out_sample_rate <= 0 || in_channels <= 0 || out_channels <= 0) return false;
    const auto in_format = static_cast<AVSampleFormat>(in_fmt);
    const auto out_format = static_cast<AVSampleFormat>(out_fmt);
    const int out_bps = av_get_bytes_per_sample(out_format);
    if (av_get_bytes_per_sample(in_format) <= 0 || out_bps <= 0) return false;
    // resample()/flush() write a single interleaved plane.
    if (out_channels > 1 && av_sample_fmt_is_planar(out_format)) return false;

    AVChannelLayout in_layout, out_layout;
    av_channel_layout_default(&in_layout, in_channels);
    av_channel_layout_default(&out_layout, out_channels);
    int ret = swr_alloc_set_opts2(&impl_->swr_,
        &out_layout, out_format, out_sample_rate,
        &in_layout, in_format, in_sample_rate, 0, nullptr);
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    if (ret < 0) { impl_->swr_ = nullptr; return false; }
    ret = swr_init(impl_->swr_);
    if (ret < 0) { swr_free(&impl_->swr_); return false; }
    impl_->out_sample_rate_ = out_sample_rate;
    impl_->out_channels_ = out_channels;
    impl_->out_fmt_ = out_format;
    impl_->out_bytes_per_sample_ = out_bps;
    return true;
}

std::vector<uint8_t> AudioResampler::resample(const Frame& frame, int& out_samples) {
    out_samples = 0;
    const AVFrame* avf = native_frame(frame);
    if (!impl_ || !impl_->swr_ || !avf || avf->nb_samples <= 0) return {};
    int max_samples = swr_get_out_samples(impl_->swr_, avf->nb_samples);
    if (max_samples < 0) return {};
    return impl_->convert(max_samples, const_cast<const uint8_t**>(avf->extended_data), avf->nb_samples, out_samples);
}

void AudioResampler::flush(std::vector<uint8_t>& output, int& out_samples) {
    out_samples = 0;
    if (!impl_ || !impl_->swr_) return;
    int max_samples = swr_get_out_samples(impl_->swr_, 0);
    if (max_samples <= 0) return;
    auto tail = impl_->convert(max_samples, nullptr, 0, out_samples);
    output.insert(output.end(), tail.begin(), tail.end());
}

} // namespace video2vec::ffmpeg
