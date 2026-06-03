#include "video2vec/ffmpeg/demuxer.hpp"
#include "video2vec/ffmpeg/media.hpp"
#include "ffmpeg_helpers.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <cstring>

namespace video2vec::ffmpeg {

class Demuxer::Impl {
public:
    AVFormatContext* fmt_ctx_ = nullptr;
    std::string path_;
    int video_stream_ = -1;
    int audio_stream_ = -1;
};

Demuxer::Demuxer() : impl_(std::make_unique<Impl>()) {}
Demuxer::~Demuxer() { close(); }
Demuxer::Demuxer(Demuxer&& other) noexcept : impl_(std::move(other.impl_)) {}
Demuxer& Demuxer::operator=(Demuxer&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

static MediaType to_media_type(int av_type) {
    switch (av_type) {
        case AVMEDIA_TYPE_VIDEO: return MediaType::Video;
        case AVMEDIA_TYPE_AUDIO: return MediaType::Audio;
        case AVMEDIA_TYPE_SUBTITLE: return MediaType::Subtitle;
        case AVMEDIA_TYPE_DATA: return MediaType::Data;
        case AVMEDIA_TYPE_ATTACHMENT: return MediaType::Attachment;
        default: return MediaType::Unknown;
    }
}

static Rational to_rational(AVRational avr) {
    return Rational{avr.num, avr.den};
}

int Demuxer::open(const std::string& path) {
    close();
    impl_->path_ = path;
    int ret = avformat_open_input(&impl_->fmt_ctx_, path.c_str(), nullptr, nullptr);
    if (ret < 0) return ret;
    ret = avformat_find_stream_info(impl_->fmt_ctx_, nullptr);
    if (ret < 0) {
        avformat_close_input(&impl_->fmt_ctx_);
        return ret;
    }
    for (unsigned int i = 0; i < impl_->fmt_ctx_->nb_streams; ++i) {
        AVStream* stream = impl_->fmt_ctx_->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && impl_->video_stream_ < 0)
            impl_->video_stream_ = static_cast<int>(i);
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && impl_->audio_stream_ < 0)
            impl_->audio_stream_ = static_cast<int>(i);
    }
    return 0;
}

void Demuxer::close() {
    if (impl_->fmt_ctx_) {
        avformat_close_input(&impl_->fmt_ctx_);
        impl_->fmt_ctx_ = nullptr;
    }
    impl_->video_stream_ = -1;
    impl_->audio_stream_ = -1;
    impl_->path_.clear();
}

bool Demuxer::is_open() const { return impl_->fmt_ctx_ != nullptr; }
std::string Demuxer::path() const { return impl_->path_; }
int Demuxer::video_stream_index() const { return impl_->video_stream_; }
int Demuxer::audio_stream_index() const { return impl_->audio_stream_; }

VideoProperties Demuxer::video_properties() const {
    VideoProperties props{};
    if (impl_->video_stream_ < 0 || !impl_->fmt_ctx_) return props;
    AVStream* st = impl_->fmt_ctx_->streams[impl_->video_stream_];
    props.width = st->codecpar->width;
    props.height = st->codecpar->height;
    props.time_base = to_rational(st->time_base);
    AVRational avg_fps = av_guess_frame_rate(impl_->fmt_ctx_, st, nullptr);
    if (avg_fps.den > 0) props.fps = av_q2d(avg_fps);
    if (st->nb_frames > 0) props.frame_count = st->nb_frames;
    else if (st->duration > 0 && avg_fps.den > 0)
        props.frame_count = static_cast<int64_t>(av_q2d(avg_fps) * st->duration * av_q2d(st->time_base));
    return props;
}

AudioProperties Demuxer::audio_properties() const {
    AudioProperties props{};
    if (impl_->audio_stream_ < 0 || !impl_->fmt_ctx_) return props;
    AVStream* st = impl_->fmt_ctx_->streams[impl_->audio_stream_];
    props.sample_rate = st->codecpar->sample_rate;
    props.channels = st->codecpar->ch_layout.nb_channels;
    props.time_base = to_rational(st->time_base);
    if (st->duration > 0)
        props.sample_count = static_cast<int64_t>(st->duration * av_q2d(st->time_base) * props.sample_rate);
    return props;
}

std::vector<StreamInfo> Demuxer::streams() const {
    std::vector<StreamInfo> result;
    if (!impl_->fmt_ctx_) return result;
    for (unsigned int i = 0; i < impl_->fmt_ctx_->nb_streams; ++i) {
        AVStream* st = impl_->fmt_ctx_->streams[i];
        StreamInfo info{};
        info.index = static_cast<int>(i);
        info.duration_pts = st->duration;
        info.duration_seconds = st->duration > 0 ? st->duration * av_q2d(st->time_base) : 0.0;
        info.width = st->codecpar->width;
        info.height = st->codecpar->height;
        info.sample_rate = st->codecpar->sample_rate;
        info.channels = st->codecpar->ch_layout.nb_channels;
        info.type = to_media_type(st->codecpar->codec_type);
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (codec) info.codec_name = codec->name;
        info.time_base = to_rational(st->time_base);
        result.push_back(std::move(info));
    }
    return result;
}

int Demuxer::read_packet(Packet& packet) {
    if (!impl_->fmt_ctx_) return AVERROR_INVALIDDATA;
    packet.unref();
    AVPacket* pkt = native_packet(packet);
    if (!pkt) return AVERROR_INVALIDDATA;
    return av_read_frame(impl_->fmt_ctx_, pkt);
}

int Demuxer::seek(int stream_index, int64_t pts, int flags) {
    if (!impl_->fmt_ctx_) return AVERROR_INVALIDDATA;
    return av_seek_frame(impl_->fmt_ctx_, stream_index, pts, flags);
}

} // namespace video2vec::ffmpeg
