#include <video2vec/ffmpeg/demuxer.hpp>
#include <video2vec/ffmpeg/decoder.hpp>
#include <video2vec/core/logger.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: " << argv[0] << " <video_file>\n"; return 1; }
    video2vec::core::Logger::initialize("basic_decode");
    video2vec::ffmpeg::Demuxer demuxer;
    int ret = demuxer.open(argv[1]);
    if (ret < 0) { std::cerr << "Failed to open video\n"; return 1; }
    std::cout << "File: " << demuxer.path() << "\n";
    auto streams = demuxer.streams();
    for (const auto& s : streams) {
        std::cout << "Stream " << s.index << ": " << s.codec_name
                  << " type=" << (s.type == AVMEDIA_TYPE_VIDEO ? "video" : "audio")
                  << " duration=" << s.duration_seconds << "s\n";
    }
    auto vprops = demuxer.video_properties();
    std::cout << "Video: " << vprops.width << "x" << vprops.height << " @ " << vprops.fps << " fps\n";
    auto aprops = demuxer.audio_properties();
    std::cout << "Audio: " << aprops.sample_rate << " Hz, " << aprops.channels << " ch\n";
    return 0;
}
