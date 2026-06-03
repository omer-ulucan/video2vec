#include "video2vec/flatbuffers/packager.hpp"
#include "video2vec/core/logger.hpp"
#include <cstring>
#include <fstream>
#include <sstream>

namespace video2vec::flatbuffers {

namespace {
    constexpr uint32_t MAGIC = 0x56454321;
    void write_u64(std::vector<uint8_t>& buf, uint64_t val) {
        for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
    void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
    void write_string(std::vector<uint8_t>& buf, const std::string& str) {
        write_u32(buf, static_cast<uint32_t>(str.size()));
        buf.insert(buf.end(), str.begin(), str.end());
    }
    uint64_t read_u64(const uint8_t*& ptr) {
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i) val |= static_cast<uint64_t>(*ptr++) << (i * 8);
        return val;
    }
    uint32_t read_u32(const uint8_t*& ptr) {
        uint32_t val = 0;
        for (int i = 0; i < 4; ++i) val |= static_cast<uint32_t>(*ptr++) << (i * 8);
        return val;
    }
    std::string read_string(const uint8_t*& ptr) {
        uint32_t len = read_u32(ptr);
        std::string str(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
        return str;
    }
}

core::Result<std::vector<uint8_t>> package_windows(const std::vector<PackagedWindow>& windows, const std::string& version) {
    std::vector<uint8_t> buffer;
    buffer.reserve(1024 * 1024);
    write_u32(buffer, MAGIC);
    write_string(buffer, version);
    write_u32(buffer, static_cast<uint32_t>(windows.size()));
    for (const auto& w : windows) {
        write_u64(buffer, static_cast<uint64_t>(w.t0_ms));
        write_u64(buffer, static_cast<uint64_t>(w.t1_ms));
        write_u32(buffer, static_cast<uint32_t>(w.index));
        write_string(buffer, w.transcript);
        write_u32(buffer, static_cast<uint32_t>(w.words.size()));
        for (const auto& word : w.words) {
            write_u64(buffer, static_cast<uint64_t>(word.t_ms));
            write_u64(buffer, static_cast<uint64_t>(word.dt_ms));
            write_string(buffer, word.text);
            uint32_t conf_bits; std::memcpy(&conf_bits, &word.confidence, sizeof(float));
            write_u32(buffer, conf_bits);
        }
        write_u32(buffer, static_cast<uint32_t>(w.frames.size()));
        for (const auto& frame : w.frames) {
            write_u64(buffer, static_cast<uint64_t>(frame.pts_ms));
            write_u32(buffer, static_cast<uint32_t>(frame.width));
            write_u32(buffer, static_cast<uint32_t>(frame.height));
            write_u32(buffer, frame.is_keyframe ? 1 : 0);
            write_u32(buffer, static_cast<uint32_t>(frame.rgb_data.size()));
            buffer.insert(buffer.end(), frame.rgb_data.begin(), frame.rgb_data.end());
        }
        write_u32(buffer, static_cast<uint32_t>(w.ocr_lines.size()));
        for (const auto& line : w.ocr_lines) {
            write_u32(buffer, static_cast<uint32_t>(line.bbox.x));
            write_u32(buffer, static_cast<uint32_t>(line.bbox.y));
            write_u32(buffer, static_cast<uint32_t>(line.bbox.w));
            write_u32(buffer, static_cast<uint32_t>(line.bbox.h));
            write_string(buffer, line.text);
        }
    }
    return core::Result<std::vector<uint8_t>>(std::move(buffer));
}

core::Result<std::vector<PackagedWindow>> unpack_windows(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return core::Result<std::vector<PackagedWindow>>(core::Error::from_code(core::ErrorCode::DecodeError, "buffer too small"));
    }
    const uint8_t* ptr = data.data();
    uint32_t magic = read_u32(ptr);
    if (magic != MAGIC) {
        return core::Result<std::vector<PackagedWindow>>(core::Error::from_code(core::ErrorCode::DecodeError, "invalid magic number"));
    }
    std::string version = read_string(ptr);
    (void)version;
    uint32_t num_windows = read_u32(ptr);
    std::vector<PackagedWindow> result;
    result.reserve(num_windows);
    for (uint32_t i = 0; i < num_windows; ++i) {
        PackagedWindow w{};
        w.t0_ms = static_cast<int64_t>(read_u64(ptr));
        w.t1_ms = static_cast<int64_t>(read_u64(ptr));
        w.index = static_cast<int>(read_u32(ptr));
        w.transcript = read_string(ptr);
        uint32_t num_words = read_u32(ptr);
        w.words.reserve(num_words);
        for (uint32_t j = 0; j < num_words; ++j) {
            asr::ASRWord word{};
            word.t_ms = static_cast<int64_t>(read_u64(ptr));
            word.dt_ms = static_cast<int64_t>(read_u64(ptr));
            word.text = read_string(ptr);
            uint32_t conf_bits = read_u32(ptr);
            std::memcpy(&word.confidence, &conf_bits, sizeof(float));
            w.words.push_back(std::move(word));
        }
        uint32_t num_frames = read_u32(ptr);
        w.frames.reserve(num_frames);
        for (uint32_t j = 0; j < num_frames; ++j) {
            vision::Frame frame{};
            frame.pts_ms = static_cast<int64_t>(read_u64(ptr));
            frame.width = static_cast<int>(read_u32(ptr));
            frame.height = static_cast<int>(read_u32(ptr));
            frame.is_keyframe = read_u32(ptr) != 0;
            uint32_t rgb_size = read_u32(ptr);
            frame.rgb_data.resize(rgb_size);
            std::memcpy(frame.rgb_data.data(), ptr, rgb_size);
            ptr += rgb_size;
            w.frames.push_back(std::move(frame));
        }
        uint32_t num_ocr = read_u32(ptr);
        w.ocr_lines.reserve(num_ocr);
        for (uint32_t j = 0; j < num_ocr; ++j) {
            ocr::OCRLine line{};
            line.bbox.x = static_cast<int>(read_u32(ptr));
            line.bbox.y = static_cast<int>(read_u32(ptr));
            line.bbox.w = static_cast<int>(read_u32(ptr));
            line.bbox.h = static_cast<int>(read_u32(ptr));
            line.text = read_string(ptr);
            w.ocr_lines.push_back(std::move(line));
        }
        result.push_back(std::move(w));
    }
    return core::Result<std::vector<PackagedWindow>>(std::move(result));
}

core::Result<void> write_index(const std::vector<PackagedWindow>& windows, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "cannot open index file: " + path));
    }
    for (const auto& w : windows) {
        file << w.t0_ms << "\t" << w.t1_ms << "\t" << w.index << "\n";
    }
    return core::Result<void>();
}

} // namespace video2vec::flatbuffers
