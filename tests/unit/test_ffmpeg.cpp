#include <gtest/gtest.h>
#include <video2vec/ffmpeg/audio_buffer.hpp>
#include <video2vec/ffmpeg/demuxer.hpp>
#include "ffmpeg/decoder_impl.hpp"
#include "ffmpeg/ffmpeg_helpers.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

using namespace video2vec::ffmpeg;

// ------------------------------------------------------------------
// Moved-from objects used to dereference a null Impl in their destructors
// and accessors. Each test moves, uses the husk, and lets it be destroyed.
// ------------------------------------------------------------------
TEST(FFmpegMove, DemuxerMovedFromIsInert) {
    Demuxer a;
    Demuxer b(std::move(a));
    EXPECT_FALSE(a.is_open());
    EXPECT_EQ(a.video_stream_index(), -1);
    EXPECT_TRUE(a.streams().empty());
    EXPECT_TRUE(a.path().empty());
    EXPECT_EQ(a.duration_ms(), 0);
    EXPECT_EQ(native_codec_parameters(a, 0), nullptr);
    Packet p;
    EXPECT_LT(a.read_packet(p), 0);
    EXPECT_LT(a.open("/nonexistent/video.mp4"), 0);
    a.close();
    EXPECT_FALSE(b.is_open());
    a = std::move(b);
    EXPECT_FALSE(a.is_open());
    EXPECT_FALSE(b.is_open());
}

TEST(FFmpegMove, DecoderMovedFromIsInert) {
    Decoder a;
    ASSERT_EQ(a.initialize("pcm_s16le", 0, 0, 16000, 1), 0);
    Decoder b(std::move(a));
    EXPECT_FALSE(a.is_initialized());
    EXPECT_TRUE(b.is_initialized());
    EXPECT_STREQ(a.codec_name(), "none");
    Frame f;
    EXPECT_LT(a.receive_frame(f), 0);
    EXPECT_LT(a.send_eof(), 0);
    a.flush();
    Decoder c;
    c = std::move(b);
    EXPECT_TRUE(c.is_initialized());
    EXPECT_FALSE(b.is_initialized());
}

TEST(FFmpegMove, AudioDecoderMovedFromIsInert) {
    AudioDecoder a;
    ASSERT_EQ(a.initialize("pcm_s16le", 8000, 1, 16000), 0);
    AudioDecoder b(std::move(a));
    EXPECT_FALSE(a.is_initialized());
    EXPECT_EQ(a.target_sample_rate(), 0);
    std::vector<float> out;
    EXPECT_LT(a.receive_samples(out), 0);
    EXPECT_EQ(a.drain_samples(out), 0);
    a.flush();
    EXPECT_TRUE(b.is_initialized());
    EXPECT_EQ(b.target_sample_rate(), 16000);
}

TEST(FFmpegMove, ResamplerMovedFromIsInert) {
    AudioResampler a;
    ASSERT_TRUE(a.initialize(44100, 2, AV_SAMPLE_FMT_S16, 16000, 1, AV_SAMPLE_FMT_FLT));
    AudioResampler b(std::move(a));
    Frame f;
    int n = 42;
    EXPECT_TRUE(a.resample(f, n).empty());
    EXPECT_EQ(n, 0);
    std::vector<uint8_t> tail;
    a.flush(tail, n);
    EXPECT_TRUE(tail.empty());
    EXPECT_FALSE(a.initialize(44100, 2, AV_SAMPLE_FMT_S16, 16000, 1, AV_SAMPLE_FMT_FLT));
}

TEST(FFmpegMove, PacketAndFrameMovedFromAreInert) {
    Packet p;
    Packet q(std::move(p));
    EXPECT_EQ(p.stream_index(), -1);
    EXPECT_TRUE(p.data().empty());
    EXPECT_FALSE(p.is_key_frame());
    EXPECT_EQ(native_packet(p), nullptr);
    p.unref();
    Frame f;
    Frame g(std::move(f));
    EXPECT_EQ(f.width(), 0);
    EXPECT_EQ(f.nb_samples(), 0);
    EXPECT_TRUE(f.data().empty());
    EXPECT_TRUE(f.linesize().empty());
    EXPECT_EQ(native_frame(f), nullptr);
    EXPECT_NE(native_packet(q), nullptr);
    EXPECT_NE(native_frame(g), nullptr);
}

// ------------------------------------------------------------------
// Re-initialization used to leak the previous codec context (visible under ASan).
// ------------------------------------------------------------------
TEST(FFmpegDecoder, InitializeTwiceReplacesContext) {
    Decoder d;
    ASSERT_EQ(d.initialize("pcm_s16le", 0, 0, 16000, 1), 0);
    ASSERT_EQ(d.initialize("pcm_s16le", 0, 0, 8000, 2), 0);
    EXPECT_TRUE(d.is_initialized());
    EXPECT_STREQ(d.codec_name(), "pcm_s16le");
    AudioDecoder ad;
    ASSERT_EQ(ad.initialize("pcm_s16le", 16000, 1, 16000), 0);
    ASSERT_EQ(ad.initialize("pcm_s16le", 8000, 1, 16000), 0);
    EXPECT_TRUE(ad.is_initialized());
}

TEST(FFmpegDecoder, RejectsUnknownCodecAndNullParams) {
    Decoder d;
    EXPECT_LT(d.initialize("no-such-codec", 0, 0, 0, 0), 0);
    EXPECT_LT(d.initialize(nullptr), 0);
    EXPECT_FALSE(d.is_initialized());
    AudioDecoder ad;
    EXPECT_LT(ad.initialize(nullptr, 16000), 0);
    EXPECT_LT(ad.initialize("pcm_s16le", 16000, 1, 0), 0);
}

// ------------------------------------------------------------------
// AudioDecoder::receive_samples decodes and resamples to mono float.
// ------------------------------------------------------------------
TEST(FFmpegAudioDecoder, ReceiveSamplesResamplesPcmToTargetRate) {
    AudioDecoder dec;
    ASSERT_EQ(dec.initialize("pcm_s16le", 8000, 1, 16000), 0);

    // 100 ms of a constant-amplitude signal at 8 kHz mono.
    const int in_samples = 800;
    Packet packet;
    AVPacket* pkt = native_packet(packet);
    ASSERT_NE(pkt, nullptr);
    ASSERT_EQ(av_new_packet(pkt, in_samples * 2), 0);
    auto* s16 = reinterpret_cast<int16_t*>(pkt->data);
    for (int i = 0; i < in_samples; ++i) s16[i] = 8192;  // 0.25 full scale

    ASSERT_EQ(dec.send_packet(packet), 0);
    std::vector<float> out;
    int total = 0;
    int n;
    while ((n = dec.receive_samples(out)) >= 0) total += n;
    EXPECT_EQ(n, AVERROR(EAGAIN));
    ASSERT_EQ(dec.send_eof(), 0);
    while ((n = dec.receive_samples(out)) >= 0) total += n;
    EXPECT_EQ(n, AVERROR_EOF);
    total += dec.drain_samples(out);

    // Upsampling 800 samples from 8 kHz to 16 kHz yields ~1600 (resampler delay aside).
    EXPECT_EQ(static_cast<int>(out.size()), total);
    EXPECT_GE(out.size(), 1500u);
    EXPECT_LE(out.size(), 1700u);
    // Steady-state value should be close to 0.25.
    EXPECT_NEAR(out[out.size() / 2], 0.25f, 0.02f);
}

// ------------------------------------------------------------------
// AudioResampler honours the output sample format's size.
// ------------------------------------------------------------------
static void fill_float_frame(Frame& frame, int samples, float value) {
    AVFrame* f = native_frame(frame);
    ASSERT_NE(f, nullptr);
    f->format = AV_SAMPLE_FMT_FLT;
    f->sample_rate = 16000;
    av_channel_layout_default(&f->ch_layout, 1);
    f->nb_samples = samples;
    ASSERT_EQ(av_frame_get_buffer(f, 0), 0);
    auto* data = reinterpret_cast<float*>(f->data[0]);
    for (int i = 0; i < samples; ++i) data[i] = value;
}

TEST(FFmpegResampler, S16OutputHasTwoBytesPerSample) {
    AudioResampler r;
    ASSERT_TRUE(r.initialize(16000, 1, AV_SAMPLE_FMT_FLT, 16000, 1, AV_SAMPLE_FMT_S16));
    Frame frame;
    fill_float_frame(frame, 1000, 0.5f);
    int n = 0;
    auto bytes = r.resample(frame, n);
    EXPECT_EQ(n, 1000);
    ASSERT_EQ(bytes.size(), 1000u * sizeof(int16_t));
    int16_t sample = 0;
    std::memcpy(&sample, bytes.data() + 500 * sizeof(int16_t), sizeof(sample));
    EXPECT_NEAR(sample, 16384, 2);
}

TEST(FFmpegResampler, FloatOutputHasFourBytesPerSample) {
    AudioResampler r;
    ASSERT_TRUE(r.initialize(16000, 1, AV_SAMPLE_FMT_FLT, 8000, 1, AV_SAMPLE_FMT_FLT));
    Frame frame;
    fill_float_frame(frame, 1000, 0.5f);
    int n = 0;
    auto bytes = r.resample(frame, n);
    EXPECT_GT(n, 0);
    EXPECT_EQ(bytes.size(), static_cast<size_t>(n) * sizeof(float));
    std::vector<uint8_t> tail;
    int tail_n = 0;
    r.flush(tail, tail_n);
    EXPECT_EQ(tail.size(), static_cast<size_t>(tail_n) * sizeof(float));
    EXPECT_NEAR(n + tail_n, 500, 5);
}

TEST(FFmpegResampler, RejectsInvalidConfigurations) {
    AudioResampler r;
    EXPECT_FALSE(r.initialize(44100, 2, AV_SAMPLE_FMT_S16, 16000, 2, AV_SAMPLE_FMT_FLTP));  // planar multi-channel
    EXPECT_FALSE(r.initialize(0, 2, AV_SAMPLE_FMT_S16, 16000, 1, AV_SAMPLE_FMT_FLT));
    EXPECT_FALSE(r.initialize(44100, 0, AV_SAMPLE_FMT_S16, 16000, 1, AV_SAMPLE_FMT_FLT));
    EXPECT_FALSE(r.initialize(44100, 2, 9999, 16000, 1, AV_SAMPLE_FMT_FLT));
    EXPECT_TRUE(r.initialize(44100, 2, AV_SAMPLE_FMT_S16, 16000, 2, AV_SAMPLE_FMT_FLT));
    EXPECT_TRUE(r.initialize(44100, 2, AV_SAMPLE_FMT_FLTP, 16000, 1, AV_SAMPLE_FMT_FLTP));  // mono planar is fine
}

// ------------------------------------------------------------------
// frame_to_rgb
// ------------------------------------------------------------------
TEST(FFmpegFrameToRgb, ConvertsYuv420ToPackedRgb) {
    Frame frame;
    AVFrame* f = native_frame(frame);
    ASSERT_NE(f, nullptr);
    f->format = AV_PIX_FMT_YUV420P;
    f->width = 32;
    f->height = 16;
    ASSERT_EQ(av_frame_get_buffer(f, 0), 0);
    std::memset(f->data[0], 128, static_cast<size_t>(f->linesize[0]) * 16);
    std::memset(f->data[1], 128, static_cast<size_t>(f->linesize[1]) * 8);
    std::memset(f->data[2], 128, static_cast<size_t>(f->linesize[2]) * 8);
    int w = 0, h = 0;
    auto rgb = frame_to_rgb(frame, w, h);
    EXPECT_EQ(w, 32);
    EXPECT_EQ(h, 16);
    ASSERT_EQ(rgb.size(), 32u * 16u * 3u);
    EXPECT_NEAR(rgb[0], 128, 8);
    EXPECT_NEAR(rgb[1], 128, 8);
    EXPECT_NEAR(rgb[2], 128, 8);
}

TEST(FFmpegFrameToRgb, RejectsHardwareAndEmptyFrames) {
    Frame frame;
    int w = 1, h = 1;
    EXPECT_TRUE(frame_to_rgb(frame, w, h).empty());  // no data allocated
    EXPECT_EQ(w, 0);
    EXPECT_EQ(h, 0);
    AVFrame* f = native_frame(frame);
    f->format = AV_PIX_FMT_VAAPI;
    f->width = 32;
    f->height = 16;
    f->data[0] = reinterpret_cast<uint8_t*>(f);  // opaque surface handle
    EXPECT_TRUE(frame_to_rgb(frame, w, h).empty());
    f->data[0] = nullptr;
    Frame moved(std::move(frame));
    EXPECT_TRUE(frame_to_rgb(frame, w, h).empty());
}

// ------------------------------------------------------------------
// AudioBuffer
// ------------------------------------------------------------------
TEST(FFmpegAudioBuffer, ZeroChannelsIsClampedToOne) {
    AudioBuffer b(16000, 0);
    EXPECT_EQ(b.channels(), 1);
    const float samples[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    b.append(std::span<const float>(samples, 4));
    EXPECT_EQ(b.sample_count(), 4u);
    AudioBuffer c(-1, -5);
    EXPECT_EQ(c.channels(), 1);
    EXPECT_EQ(c.sample_rate(), 16000);
}

TEST(FFmpegAudioBuffer, Int16RoundTrip) {
    AudioBuffer b(16000, 1);
    std::vector<int16_t> in = {0, 16384, -32768, 32767, -1};
    b.append(std::span<const int16_t>(in.data(), in.size()));
    EXPECT_EQ(b.sample_count(), in.size());
    auto out = b.to_int16();
    EXPECT_EQ(out, in);
    const float loud[2] = {2.0f, -2.0f};
    b.clear();
    b.append(std::span<const float>(loud, 2));
    auto clamped = b.to_int16();
    ASSERT_EQ(clamped.size(), 2u);
    EXPECT_EQ(clamped[0], 32767);
    EXPECT_EQ(clamped[1], -32768);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
