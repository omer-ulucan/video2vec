#pragma once

#include "video2vec/ffmpeg/media.hpp"

struct AVPacket;
struct AVFrame;

namespace video2vec::ffmpeg {

AVPacket* native_packet(Packet& p);
const AVPacket* native_packet(const Packet& p);
AVFrame* native_frame(Frame& f);
const AVFrame* native_frame(const Frame& f);

} // namespace video2vec::ffmpeg
