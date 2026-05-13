#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

static inline uint16_t bswap16(uint16_t x) {
    return static_cast<uint16_t>((x >> 8) | (x << 8));
}

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF000000u) >> 24) |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x000000FFu) << 24);
}

static inline uint64_t bswap64(uint64_t x) {
    return ((x & 0xFF00000000000000ull) >> 56) |
           ((x & 0x00FF000000000000ull) >> 40) |
           ((x & 0x0000FF0000000000ull) >> 24) |
           ((x & 0x000000FF00000000ull) >> 8)  |
           ((x & 0x00000000FF000000ull) << 8)  |
           ((x & 0x0000000000FF0000ull) << 24) |
           ((x & 0x000000000000FF00ull) << 40) |
           ((x & 0x00000000000000FFull) << 56);
}

static inline void write_be16(std::ostream& os, uint16_t v) {
    v = bswap16(v);
    os.write(reinterpret_cast<const char*>(&v), 2);
}

static inline void write_be32(std::ostream& os, uint32_t v) {
    v = bswap32(v);
    os.write(reinterpret_cast<const char*>(&v), 4);
}

static inline void write_be64(std::ostream& os, uint64_t v) {
    v = bswap64(v);
    os.write(reinterpret_cast<const char*>(&v), 8);
}

static inline void write_le16(std::ostream& os, uint16_t v) {
    os.write(reinterpret_cast<const char*>(&v), 2);
}

static inline void write_le32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

static inline void write_bytes(std::ostream& os, const void* data, size_t len) {
    os.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
}

static inline void write_fourcc(std::ostream& os, const char (&s)[5]) {
    os.write(s, 4);
}

static inline void write_zeros(std::ostream& os, size_t count) {
    for (size_t i = 0; i < count; ++i)
        os.put(0);
}

static inline uint16_t read_be16(std::istream& is) {
    uint16_t v;
    is.read(reinterpret_cast<char*>(&v), 2);
    return bswap16(v);
}

static inline uint32_t read_be32(std::istream& is) {
    uint32_t v;
    is.read(reinterpret_cast<char*>(&v), 4);
    return bswap32(v);
}

static inline uint16_t read_le16(std::istream& is) {
    uint16_t v;
    is.read(reinterpret_cast<char*>(&v), 2);
    return v;
}

static inline uint32_t read_le32(std::istream& is) {
    uint32_t v;
    is.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

static inline uint64_t read_be64(std::istream& is) {
    uint64_t v;
    is.read(reinterpret_cast<char*>(&v), 8);
    return bswap64(v);
}

static inline void read_bytes(std::istream& is, void* buf, size_t len) {
    is.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
}

static inline void skip_bytes(std::istream& is, size_t count) {
    is.seekg(static_cast<std::streamoff>(count), std::ios::cur);
}

enum class AstCodecId : uint16_t {
    ADPCM_AFC        = 0,
    PCM_S16BE_PLANAR = 1,
};

struct AstHeader {
    AstCodecId codec_id;
    uint16_t   bit_depth;
    uint16_t   channels;
    uint16_t   loop_flag;
    uint32_t   sample_rate;
    uint32_t   num_samples;
    uint32_t   loop_start;
    uint32_t   loop_end;
    uint32_t   first_block_size;
};

static inline AstHeader ast_read_header(std::istream& is) {
    char magic[4];
    is.read(magic, 4);
    if (magic[0] != 'S' || magic[1] != 'T' || magic[2] != 'R' || magic[3] != 'M')
        throw std::runtime_error("Not a valid AST file: missing STRM magic");

    AstHeader h{};
    skip_bytes(is, 4);
    h.codec_id         = static_cast<AstCodecId>(read_be16(is));
    h.bit_depth        = read_be16(is);
    h.channels         = read_be16(is);
    h.loop_flag        = read_be16(is);
    h.sample_rate      = read_be32(is);
    h.num_samples      = read_be32(is);
    h.loop_start       = read_be32(is);
    h.loop_end         = read_be32(is);
    h.first_block_size = read_be32(is);
    skip_bytes(is, 28);

    if (h.bit_depth != 16)
        throw std::runtime_error("Unsupported bit depth: only 16-bit is supported");
    if (h.channels == 0)
        throw std::runtime_error("Invalid channel count: 0");
    if (h.sample_rate == 0)
        throw std::runtime_error("Invalid sample rate: 0");

    return h;
}

static inline void ast_write_header(std::ostream& os, AstCodecId codec_id,
                                     uint16_t channels, uint32_t sample_rate,
                                     int64_t loop_start_ms, int64_t loop_end_ms) {
    if (loop_end_ms > 0 && loop_start_ms >= loop_end_ms)
        throw std::runtime_error("loopend can't be less or equal to loopstart");

    write_fourcc(os, "STRM");
    write_be32(os, 0);
    write_be16(os, static_cast<uint16_t>(codec_id));
    write_be16(os, 16);
    write_be16(os, channels);
    write_be16(os, 0);
    write_be32(os, sample_rate);
    write_be32(os, 0);
    write_be32(os, 0);
    write_be32(os, 0);
    write_be32(os, 0);
    write_be32(os, 0);
    write_le32(os, 0x7F);
    write_be64(os, 0);
    write_be64(os, 0);
    write_be32(os, 0);
}

static inline AstHeader ast_decode(const std::string& path, std::vector<int16_t>& pcm_interleaved) {
    std::ifstream fs(path, std::ios::binary);
    if (!fs)
        throw std::runtime_error("Cannot open file: " + path);

    AstHeader hdr = ast_read_header(fs);
    pcm_interleaved.clear();

    size_t sample_bytes = hdr.bit_depth / 8;

    while (fs && !fs.eof()) {
        char chunk_hdr[8];
        fs.read(chunk_hdr, 8);
        if (fs.gcount() < 8) break;

        uint32_t chunk_type;
        std::memcpy(&chunk_type, chunk_hdr, 4);
        uint32_t block_size = bswap32(*reinterpret_cast<const uint32_t*>(chunk_hdr + 4));

        skip_bytes(fs, 24);

        if (chunk_type == 0x4B434C42) {
            size_t frames_per_ch = block_size / sample_bytes;
            size_t total_bytes = static_cast<size_t>(block_size) * hdr.channels;
            std::vector<uint8_t> block_data(total_bytes);
            read_bytes(fs, block_data.data(), total_bytes);

            size_t old_frames = pcm_interleaved.size() / hdr.channels;
            pcm_interleaved.resize((old_frames + frames_per_ch) * hdr.channels);

            for (size_t ch = 0; ch < hdr.channels; ++ch) {
                for (size_t i = 0; i < frames_per_ch; ++i) {
                    uint16_t raw;
                    std::memcpy(&raw, block_data.data() + (ch * frames_per_ch + i) * sample_bytes, sample_bytes);
                    int16_t sample = static_cast<int16_t>(bswap16(raw));
                    pcm_interleaved[(old_frames + i) * hdr.channels + ch] = sample;
                }
            }
        } else {
            skip_bytes(fs, static_cast<size_t>(block_size) * hdr.channels);
        }
    }

    return hdr;
}

static inline void ast_encode(const std::string& path,
                               const int16_t* pcm_interleaved,
                               size_t num_frames,
                               uint16_t channels,
                               uint32_t sample_rate,
                               int64_t loop_start_ms = -1,
                               int64_t loop_end_ms = 0) {
    std::ofstream fs(path, std::ios::binary);
    if (!fs)
        throw std::runtime_error("Cannot create file: " + path);

    ast_write_header(fs, AstCodecId::PCM_S16BE_PLANAR, channels, sample_rate,
                     loop_start_ms, loop_end_ms);

    constexpr size_t kBlockFrames = 1024;
    constexpr size_t kSampleBytes = sizeof(int16_t);
    size_t written_frames = 0;
    uint32_t first_block_size = 0;
    uint32_t frame_count = 0;

    std::vector<uint8_t> planar_block(kBlockFrames * channels * kSampleBytes);

    while (written_frames < num_frames) {
        size_t cur = kBlockFrames;
        if (cur > num_frames - written_frames)
            cur = num_frames - written_frames;

        for (size_t ch = 0; ch < channels; ++ch) {
            for (size_t i = 0; i < cur; ++i) {
                size_t src_idx = (written_frames + i) * channels + ch;
                size_t dst_idx = ch * cur + i;
                uint16_t raw = bswap16(static_cast<uint16_t>(pcm_interleaved[src_idx]));
                std::memcpy(planar_block.data() + dst_idx * kSampleBytes,
                            &raw, kSampleBytes);
            }
        }

        write_fourcc(fs, "BLCK");
        write_be32(fs, static_cast<uint32_t>(cur * kSampleBytes));
        write_zeros(fs, 24);
        write_bytes(fs, planar_block.data(), cur * channels * kSampleBytes);

        if (frame_count == 0)
            first_block_size = static_cast<uint32_t>(cur * kSampleBytes);

        written_frames += cur;
        ++frame_count;
    }

    int64_t file_size = fs.tellp();

    int64_t ls = loop_start_ms;
    int64_t le = loop_end_ms;

    if (ls > 0) {
        ls = ls * sample_rate / 1000;
        if (ls < 0 || ls > 0xFFFFFFFF)
            ls = -1;
    }
    if (le > 0) {
        le = le * sample_rate / 1000;
        if (le < 0 || le > 0xFFFFFFFF)
            le = static_cast<int64_t>(num_frames);
    }

    fs.seekp(20, std::ios::beg);
    write_be32(fs, static_cast<uint32_t>(num_frames));

    if (ls > 0) {
        write_be32(fs, static_cast<uint32_t>(ls));
    } else {
        write_be32(fs, 0);
    }

    if (le > 0 && ls >= 0) {
        write_be32(fs, static_cast<uint32_t>(le));
    } else {
        write_be32(fs, static_cast<uint32_t>(num_frames));
    }

    write_be32(fs, first_block_size);

    fs.seekp(4, std::ios::beg);
    write_be32(fs, static_cast<uint32_t>(file_size - 64));

    if (ls >= 0) {
        fs.seekp(14, std::ios::beg);
        write_be16(fs, 0xFFFF);
    }

    fs.seekp(file_size, std::ios::beg);
}
