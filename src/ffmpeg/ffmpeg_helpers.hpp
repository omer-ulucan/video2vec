#pragma once

#include "video2vec/ffmpeg/demuxer.hpp"
#include "video2vec/ffmpeg/media.hpp"

struct AVPacket;
struct AVFrame;
struct AVCodecParameters;

namespace video2vec::ffmpeg {

// Internal accessors that unwrap the opaque public handles. Each returns
// nullptr for a moved-from (empty) object.
AVPacket* native_packet(Packet& p);
const AVPacket* native_packet(const Packet& p);
AVFrame* native_frame(Frame& f);
const AVFrame* native_frame(const Frame& f);

// Codec parameters of a demuxed stream, or nullptr if the demuxer is not open
// or the index is out of range. The pointer is owned by the demuxer and is
// valid until it is closed.
const AVCodecParameters* native_codec_parameters(const Demuxer& demuxer, int stream_index);

} // namespace video2vec::ffmpeg
