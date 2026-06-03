#include <video2vec/ffmpeg/demuxer.hpp>
#include <video2vec/core/logger.hpp>
#include <iostream>

int main() {
    video2vec::core::Logger::initialize("consumer_test");
    video2vec::ffmpeg::Demuxer demuxer;
    std::cout << "find_package(video2vec) works!" << std::endl;
    return 0;
}
