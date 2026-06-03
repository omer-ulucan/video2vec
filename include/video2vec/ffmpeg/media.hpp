#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "video2vec/ffmpeg/types.hpp"

namespace video2vec::ffmpeg {

// Opaque packet handle. Internal representation is AVPacket.
class Packet {
public:
    Packet();
    ~Packet();
    Packet(Packet&&) noexcept;
    Packet& operator=(Packet&&) noexcept;
    Packet(const Packet&) = delete;
    Packet& operator=(const Packet&) = delete;

    [[nodiscard]] int stream_index() const;
    [[nodiscard]] int64_t pts() const;
    [[nodiscard]] int64_t dts() const;
    [[nodiscard]] std::span<const uint8_t> data() const;
    [[nodiscard]] bool is_key_frame() const;
    void unref();

    struct Impl;
    Impl* get() const;
private:
    std::unique_ptr<Impl> impl_;
};

// Opaque frame handle. Internal representation is AVFrame.
class Frame {
public:
    Frame();
    ~Frame();
    Frame(Frame&&) noexcept;
    Frame& operator=(Frame&&) noexcept;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int sample_rate() const;
    [[nodiscard]] int channels() const;
    [[nodiscard]] int nb_samples() const;
    [[nodiscard]] int64_t pts() const;
    [[nodiscard]] int format() const; // opaque raw format integer
    [[nodiscard]] std::span<const uint8_t*> data() const;
    [[nodiscard]] std::span<const int> linesize() const;

    struct Impl;
    Impl* get() const;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ffmpeg
