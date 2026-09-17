#include "video2vec/flatbuffers/packager.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace video2vec::flatbuffers {

namespace {
    constexpr uint32_t MAGIC = 0x56454321;

    // ---------------------------------------------------------------- writer
    void write_u8(std::vector<uint8_t>& buf, uint8_t val) { buf.push_back(val); }
    void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
    void write_u64(std::vector<uint8_t>& buf, uint64_t val) {
        for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
    void write_f32(std::vector<uint8_t>& buf, float val) {
        uint32_t bits; std::memcpy(&bits, &val, sizeof(bits)); write_u32(buf, bits);
    }
    void write_f64(std::vector<uint8_t>& buf, double val) {
        uint64_t bits; std::memcpy(&bits, &val, sizeof(bits)); write_u64(buf, bits);
    }
    void write_bytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
        write_u32(buf, static_cast<uint32_t>(len));
        buf.insert(buf.end(), data, data + len);
    }
    void write_string(std::vector<uint8_t>& buf, const std::string& str) {
        write_bytes(buf, reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }

    // ---------------------------------------------------------------- reader
    // Bounds-checked cursor: every read verifies the remaining input first.
    // A failed read sets `error` and leaves subsequent reads as no-ops, so
    // callers can check once per record.
    class Reader {
    public:
        Reader(const uint8_t* data, size_t size) : begin_(data), ptr_(data), end_(data + size) {}

        bool ok() const { return error_.empty(); }
        const std::string& error() const { return error_; }
        size_t remaining() const { return static_cast<size_t>(end_ - ptr_); }

        bool u8(uint8_t& out) { if (!need(1)) return false; out = *ptr_++; return true; }
        bool u32(uint32_t& out) {
            if (!need(4)) return false;
            out = 0;
            for (int i = 0; i < 4; ++i) out |= static_cast<uint32_t>(*ptr_++) << (i * 8);
            return true;
        }
        bool u64(uint64_t& out) {
            if (!need(8)) return false;
            out = 0;
            for (int i = 0; i < 8; ++i) out |= static_cast<uint64_t>(*ptr_++) << (i * 8);
            return true;
        }
        bool i64(int64_t& out) { uint64_t v; if (!u64(v)) return false; out = static_cast<int64_t>(v); return true; }
        bool f32(float& out) { uint32_t bits; if (!u32(bits)) return false; std::memcpy(&out, &bits, sizeof(out)); return true; }
        bool f64(double& out) { uint64_t bits; if (!u64(bits)) return false; std::memcpy(&out, &bits, sizeof(out)); return true; }
        bool str(std::string& out) {
            uint32_t len;
            if (!u32(len) || !need(len)) return false;
            out.assign(reinterpret_cast<const char*>(ptr_), len);
            ptr_ += len;
            return true;
        }
        bool bytes(std::vector<uint8_t>& out) {
            uint32_t len;
            if (!u32(len) || !need(len)) return false;
            out.assign(ptr_, ptr_ + len);
            ptr_ += len;
            return true;
        }
        // Reads an element count and checks that at least `min_bytes_each`
        // bytes per element remain, so a corrupted count cannot drive a huge
        // reserve() or a long loop over a short buffer.
        bool count(uint32_t& out, size_t min_bytes_each) {
            if (!u32(out)) return false;
            if (min_bytes_each > 0 && out > remaining() / min_bytes_each) {
                fail("element count " + std::to_string(out) + " exceeds the remaining input");
                return false;
            }
            return true;
        }

    private:
        bool need(size_t n) {
            if (!ok()) return false;
            if (remaining() < n) {
                fail("truncated input: need " + std::to_string(n) + " bytes, have " + std::to_string(remaining()));
                return false;
            }
            return true;
        }
        void fail(const std::string& why) {
            if (error_.empty()) error_ = why + " at offset " + std::to_string(ptr_ - begin_);
        }
        const uint8_t* begin_;
        const uint8_t* ptr_;
        const uint8_t* end_;
        std::string error_;
    };

    template <typename T>
    core::Result<T> decode_error(const std::string& what) {
        return core::Result<T>(core::Error::from_code(core::ErrorCode::DecodeError, what));
    }
}

core::Result<std::vector<uint8_t>> package_windows(const std::vector<PackagedWindow>& windows, const std::string& version) {
    std::vector<uint8_t> buffer;
    buffer.reserve(1024 * 1024);
    write_u32(buffer, MAGIC);
    write_u32(buffer, kVecFormatVersion);
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
            write_f64(buffer, word.confidence);
        }

        write_u32(buffer, static_cast<uint32_t>(w.frames.size()));
        for (const auto& frame : w.frames) {
            write_u64(buffer, static_cast<uint64_t>(frame.pts_ms));
            write_u32(buffer, static_cast<uint32_t>(frame.width));
            write_u32(buffer, static_cast<uint32_t>(frame.height));
            write_u8(buffer, frame.is_keyframe ? 1 : 0);
            write_f64(buffer, frame.entropy);
            write_f64(buffer, frame.ocr_density);
            write_f64(buffer, frame.score);
            write_bytes(buffer, frame.rgb_data.data(), frame.rgb_data.size());
        }

        write_u32(buffer, static_cast<uint32_t>(w.ocr_lines.size()));
        for (const auto& line : w.ocr_lines) {
            write_u32(buffer, static_cast<uint32_t>(line.bbox.x));
            write_u32(buffer, static_cast<uint32_t>(line.bbox.y));
            write_u32(buffer, static_cast<uint32_t>(line.bbox.w));
            write_u32(buffer, static_cast<uint32_t>(line.bbox.h));
            write_string(buffer, line.text);
            write_f64(buffer, line.confidence);
            write_u8(buffer, line.pii_flagged ? 1 : 0);
        }

        write_u32(buffer, static_cast<uint32_t>(w.embeddings.size()));
        for (const auto& emb : w.embeddings) {
            write_u8(buffer, static_cast<uint8_t>(emb.type));
            write_u8(buffer, static_cast<uint8_t>(emb.quant));
            write_u32(buffer, static_cast<uint32_t>(emb.dim));
            write_f32(buffer, emb.int8_scale);
            write_bytes(buffer, reinterpret_cast<const uint8_t*>(emb.int8_data.data()), emb.int8_data.size());
            write_u32(buffer, static_cast<uint32_t>(emb.float_data.size()));
            for (float v : emb.float_data) write_f32(buffer, v);
        }

        write_u64(buffer, static_cast<uint64_t>(w.processing_time_ms));
    }
    return core::Result<std::vector<uint8_t>>(std::move(buffer));
}

core::Result<std::vector<PackagedWindow>> unpack_windows(const std::vector<uint8_t>& data) {
    using R = core::Result<std::vector<PackagedWindow>>;
    Reader in(data.data(), data.size());

    uint32_t magic = 0;
    if (!in.u32(magic)) return decode_error<std::vector<PackagedWindow>>("buffer too small for a .vec header");
    if (magic != MAGIC) return decode_error<std::vector<PackagedWindow>>("invalid magic number");
    uint32_t format = 0;
    if (!in.u32(format)) return decode_error<std::vector<PackagedWindow>>(in.error());
    if (format != kVecFormatVersion) {
        return decode_error<std::vector<PackagedWindow>>("unsupported .vec format version " + std::to_string(format) +
                                                          " (this build reads version " + std::to_string(kVecFormatVersion) + ")");
    }
    std::string producer;
    if (!in.str(producer)) return decode_error<std::vector<PackagedWindow>>(in.error());

    // Minimum encoded sizes, used to bound reserve() and reject bogus counts.
    constexpr size_t kMinWindow = 8 + 8 + 4 + 4 + 4 + 4 + 4 + 4 + 8;
    constexpr size_t kMinWord = 8 + 8 + 4 + 8;
    constexpr size_t kMinFrame = 8 + 4 + 4 + 1 + 8 + 8 + 8 + 4;
    constexpr size_t kMinOcr = 4 * 4 + 4 + 8 + 1;
    constexpr size_t kMinEmbedding = 1 + 1 + 4 + 4 + 4 + 4;

    uint32_t num_windows = 0;
    if (!in.count(num_windows, kMinWindow)) return decode_error<std::vector<PackagedWindow>>(in.error());
    std::vector<PackagedWindow> result;
    result.reserve(num_windows);

    for (uint32_t i = 0; i < num_windows; ++i) {
        PackagedWindow w{};
        in.i64(w.t0_ms);
        in.i64(w.t1_ms);
        uint32_t index = 0;
        in.u32(index);
        w.index = static_cast<int>(index);
        in.str(w.transcript);

        uint32_t num_words = 0;
        if (in.count(num_words, kMinWord)) w.words.reserve(num_words);
        for (uint32_t j = 0; j < num_words && in.ok(); ++j) {
            asr::ASRWord word{};
            in.i64(word.t_ms);
            in.i64(word.dt_ms);
            in.str(word.text);
            in.f64(word.confidence);
            w.words.push_back(std::move(word));
        }

        uint32_t num_frames = 0;
        if (in.count(num_frames, kMinFrame)) w.frames.reserve(num_frames);
        for (uint32_t j = 0; j < num_frames && in.ok(); ++j) {
            vision::Frame frame{};
            uint32_t width = 0, height = 0;
            uint8_t keyframe = 0;
            in.i64(frame.pts_ms);
            in.u32(width);
            in.u32(height);
            in.u8(keyframe);
            in.f64(frame.entropy);
            in.f64(frame.ocr_density);
            in.f64(frame.score);
            in.bytes(frame.rgb_data);
            frame.width = static_cast<int>(width);
            frame.height = static_cast<int>(height);
            frame.is_keyframe = keyframe != 0;
            w.frames.push_back(std::move(frame));
        }

        uint32_t num_ocr = 0;
        if (in.count(num_ocr, kMinOcr)) w.ocr_lines.reserve(num_ocr);
        for (uint32_t j = 0; j < num_ocr && in.ok(); ++j) {
            ocr::OCRLine line{};
            uint32_t x = 0, y = 0, bw = 0, bh = 0;
            uint8_t pii = 0;
            in.u32(x); in.u32(y); in.u32(bw); in.u32(bh);
            in.str(line.text);
            in.f64(line.confidence);
            in.u8(pii);
            line.bbox = {static_cast<int>(x), static_cast<int>(y), static_cast<int>(bw), static_cast<int>(bh)};
            line.pii_flagged = pii != 0;
            w.ocr_lines.push_back(std::move(line));
        }

        uint32_t num_emb = 0;
        if (in.count(num_emb, kMinEmbedding)) w.embeddings.reserve(num_emb);
        for (uint32_t j = 0; j < num_emb && in.ok(); ++j) {
            embedding::Embedding emb{};
            uint8_t type = 0, quant = 0;
            uint32_t dim = 0;
            in.u8(type); in.u8(quant); in.u32(dim);
            in.f32(emb.int8_scale);
            std::vector<uint8_t> int8_bytes;
            in.bytes(int8_bytes);
            uint32_t num_floats = 0;
            if (in.count(num_floats, 4)) {
                emb.float_data.resize(num_floats);
                for (uint32_t k = 0; k < num_floats && in.ok(); ++k) in.f32(emb.float_data[k]);
            }
            emb.type = static_cast<embedding::EmbeddingType>(type);
            emb.quant = static_cast<embedding::Quantization>(quant);
            emb.dim = static_cast<int>(dim);
            emb.int8_data.assign(reinterpret_cast<const int8_t*>(int8_bytes.data()),
                                 reinterpret_cast<const int8_t*>(int8_bytes.data()) + int8_bytes.size());
            w.embeddings.push_back(std::move(emb));
        }

        in.i64(w.processing_time_ms);
        if (!in.ok()) {
            return decode_error<std::vector<PackagedWindow>>("window " + std::to_string(i) + ": " + in.error());
        }
        result.push_back(std::move(w));
    }
    if (in.remaining() != 0) {
        core::Logger::warn("unpack_windows: " + std::to_string(in.remaining()) + " trailing bytes ignored", {});
    }
    return R(std::move(result));
}

core::Result<void> write_index(const std::vector<PackagedWindow>& windows, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "cannot open index file: " + path));
    }
    for (const auto& w : windows) {
        file << w.t0_ms << "\t" << w.t1_ms << "\t" << w.index << "\n";
    }
    file.close();
    if (!file.good()) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "incomplete write to index file: " + path));
    }
    return core::Result<void>();
}

} // namespace video2vec::flatbuffers
