#include "video2vec/ffmpeg/media.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace video2vec::ffmpeg {

// ------------------------------------------------------------------
// Packet
// ------------------------------------------------------------------
struct Packet::Impl {
    AVPacket* pkt = nullptr;
    Impl() : pkt(av_packet_alloc()) {}
    ~Impl() { if (pkt) av_packet_free(&pkt); }
};

Packet::Packet() : impl_(std::make_unique<Impl>()) {}
Packet::~Packet() = default;
Packet::Packet(Packet&&) noexcept = default;
Packet& Packet::operator=(Packet&&) noexcept = default;

int Packet::stream_index() const { return impl_->pkt ? impl_->pkt->stream_index : -1; }
int64_t Packet::pts() const { return impl_->pkt ? impl_->pkt->pts : AV_NOPTS_VALUE; }
int64_t Packet::dts() const { return impl_->pkt ? impl_->pkt->dts : AV_NOPTS_VALUE; }
std::span<const uint8_t> Packet::data() const {
    if (!impl_->pkt || !impl_->pkt->data || impl_->pkt->size <= 0) return {};
    return std::span<const uint8_t>(impl_->pkt->data, impl_->pkt->size);
}
bool Packet::is_key_frame() const { return impl_->pkt ? (impl_->pkt->flags & AV_PKT_FLAG_KEY) != 0 : false; }
void Packet::unref() { if (impl_->pkt) av_packet_unref(impl_->pkt); }
Packet::Impl* Packet::get() const { return impl_.get(); }

// ------------------------------------------------------------------
// Frame
// ------------------------------------------------------------------
struct Frame::Impl {
    AVFrame* frame = nullptr;
    Impl() : frame(av_frame_alloc()) {}
    ~Impl() { if (frame) av_frame_free(&frame); }
};

Frame::Frame() : impl_(std::make_unique<Impl>()) {}
Frame::~Frame() = default;
Frame::Frame(Frame&&) noexcept = default;
Frame& Frame::operator=(Frame&&) noexcept = default;

int Frame::width() const { return impl_->frame ? impl_->frame->width : 0; }
int Frame::height() const { return impl_->frame ? impl_->frame->height : 0; }
int Frame::sample_rate() const { return impl_->frame ? impl_->frame->sample_rate : 0; }
int Frame::channels() const { return impl_->frame ? impl_->frame->ch_layout.nb_channels : 0; }
int Frame::nb_samples() const { return impl_->frame ? impl_->frame->nb_samples : 0; }
int64_t Frame::pts() const { return impl_->frame ? impl_->frame->pts : AV_NOPTS_VALUE; }
int Frame::format() const { return impl_->frame ? impl_->frame->format : -1; }
std::span<const uint8_t*> Frame::data() const {
    if (!impl_->frame) return {};
    return std::span<const uint8_t*>(const_cast<const uint8_t**>(impl_->frame->data), AV_NUM_DATA_POINTERS);
}
std::span<const int> Frame::linesize() const {
    if (!impl_->frame) return {};
    return std::span<const int>(impl_->frame->linesize, AV_NUM_DATA_POINTERS);
}
Frame::Impl* Frame::get() const { return impl_.get(); }

AVPacket* native_packet(Packet& p) { return p.get() ? p.get()->pkt : nullptr; }
const AVPacket* native_packet(const Packet& p) { return p.get() ? p.get()->pkt : nullptr; }
AVFrame* native_frame(Frame& f) { return f.get() ? f.get()->frame : nullptr; }
const AVFrame* native_frame(const Frame& f) { return f.get() ? f.get()->frame : nullptr; }

} // namespace video2vec::ffmpeg
