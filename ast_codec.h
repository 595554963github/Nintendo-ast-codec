#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <cmath>

static inline uint16_t bswap16(uint16_t x) {
    return static_cast<uint16_t>((x >> 8) | (x << 8));
}

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF000000u) >> 24) |
        ((x & 0x00FF0000u) >> 8) |
        ((x & 0x0000FF00u) << 8) |
        ((x & 0x000000FFu) << 24);
}

static inline uint64_t bswap64(uint64_t x) {
    return ((x & 0xFF00000000000000ull) >> 56) |
        ((x & 0x00FF000000000000ull) >> 40) |
        ((x & 0x0000FF0000000000ull) >> 24) |
        ((x & 0x000000FF00000000ull) >> 8) |
        ((x & 0x00000000FF000000ull) << 8) |
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

static inline void write_fourcc(std::ostream& os, const char(&s)[5]) {
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
    ADPCM_AFC = 0,
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

// AFC ADPCM coefficients
static const int AFC_COEFS[16][2] = {
    {    0,    0 },
    { 2048,    0 },
    {    0, 2048 },
    { 1024, 1024 },
    { 4096,-2048 },
    { 3584,-1536 },
    { 3072,-1024 },
    { 4608,-2560 },
    { 4200,-2248 },
    { 4800,-2300 },
    { 5120,-3072 },
    { 2048,-2048 },
    { 1024,-1024 },
    {-1024, 1024 },
    {-1024,    0 },
    {-2048,    0 }
};

static const int AFC_PRE_SCALE[16] = {
    1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};

static inline int clamp16(int v) {
    return v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
}

// Encode a single AFC frame (16 samples)
static void encode_afc_frame(const int16_t samples[16], int& hist1, int& hist2, uint8_t output[9]) {
    int best_idx = 0, best_sc = 0;
    int64_t best_err = INT64_MAX;
    int best_nibbles[16] = { 0 };
    int nibbles[16] = { 0 };

    for (int idx = 0; idx < 16; idx++) {
        int c1 = AFC_COEFS[idx][0];
        int c2 = AFC_COEFS[idx][1];

        for (int sc = 0; sc < 16; sc++) {
            int scale = AFC_PRE_SCALE[sc];
            int64_t err = 0;
            int h1 = hist1, h2 = hist2;
            bool dead = false;

            for (int i = 0; i < 16; i++) {
                int target = samples[i];
                int64_t predictor = (int64_t)c1 * h1 + (int64_t)c2 * h2;
                int64_t raw = ((int64_t)target << 11) - predictor;
                int n = (int)(raw / ((int64_t)scale << 11));

                if (n > 7) n = 7;
                if (n < -8) n = -8;
                nibbles[i] = n;

                int recon = (int)(((int64_t)n * scale << 11) + predictor) >> 11;
                recon = clamp16(recon);
                err += (int64_t)(target - recon) * (target - recon);
                h2 = h1;
                h1 = recon;

                if (err > best_err) {
                    dead = true;
                    break;
                }
            }

            if (!dead && err < best_err) {
                best_err = err;
                best_idx = idx;
                best_sc = sc;
                memcpy(best_nibbles, nibbles, sizeof(best_nibbles));
                if (err == 0) goto done;
            }
        }
    }

done:
    output[0] = (uint8_t)((best_sc << 4) | best_idx);

    int h1 = hist1, h2 = hist2;
    int c1 = AFC_COEFS[best_idx][0];
    int c2 = AFC_COEFS[best_idx][1];
    int scale = AFC_PRE_SCALE[best_sc];

    for (int i = 0; i < 16; i++) {
        int n = best_nibbles[i];
        int r = ((n * scale) << 11) + c1 * h1 + c2 * h2;
        r >>= 11;
        r = clamp16(r);
        h2 = h1;
        h1 = r;

        if ((i & 1) == 0) {
            output[1 + i / 2] = (uint8_t)((n & 0x0F) << 4);
        }
        else {
            output[1 + i / 2] |= (uint8_t)(n & 0x0F);
        }
    }

    hist1 = h1;
    hist2 = h2;
}

// Encode a block of samples using AFC ADPCM
static std::vector<uint8_t> encode_afc_block(const int16_t* pcm, int num_samples, int& hist1, int& hist2) {
    int padded = ((num_samples + 15) / 16) * 16;
    std::vector<int16_t> buf(padded, 0);
    memcpy(buf.data(), pcm, num_samples * sizeof(int16_t));

    int fc = padded / 16;
    std::vector<uint8_t> result(fc * 9);

    for (int f = 0; f < fc; f++) {
        int16_t slice[16];
        memcpy(slice, buf.data() + f * 16, sizeof(slice));
        encode_afc_frame(slice, hist1, hist2, result.data() + f * 9);
    }

    return result;
}

// Decode a single AFC frame (16 samples)
static void decode_afc_frame(const uint8_t frame[9], int& hist1, int& hist2, int16_t output[16]) {
    int scale = 1 << ((frame[0] >> 4) & 0x0F);
    int index = frame[0] & 0x0F;
    int c1 = AFC_COEFS[index][0];
    int c2 = AFC_COEFS[index][1];

    for (int i = 0; i < 16; i++) {
        uint8_t code = frame[1 + i / 2];
        int n = (i & 1) == 0 ? (code >> 4) : (code & 0x0F);
        if (n >= 8) n -= 16;

        int s = ((n * scale) << 11) + c1 * hist1 + c2 * hist2;
        s >>= 11;
        s = clamp16(s);
        output[i] = (int16_t)s;
        hist2 = hist1;
        hist1 = s;
    }
}

// Decode an AFC block
static std::vector<int16_t> decode_afc_block(const uint8_t* data, size_t size, int& hist1, int& hist2) {
    int fc = (int)size / 9;
    std::vector<int16_t> pcm(fc * 16);
    size_t pos = 0;

    for (int f = 0; f < fc; f++) {
        int16_t samples[16];
        decode_afc_frame(data + f * 9, hist1, hist2, samples);
        for (int i = 0; i < 16 && pos < pcm.size(); i++) {
            pcm[pos++] = samples[i];
        }
    }

    return pcm;
}

static inline AstHeader ast_read_header(std::istream& is) {
    char magic[4];
    is.read(magic, 4);
    if (magic[0] != 'S' || magic[1] != 'T' || magic[2] != 'R' || magic[3] != 'M')
        throw std::runtime_error("Not a valid AST file: missing STRM magic");

    AstHeader h{};
    skip_bytes(is, 4);
    h.codec_id = static_cast<AstCodecId>(read_be16(is));
    h.bit_depth = read_be16(is);
    h.channels = read_be16(is);
    h.loop_flag = read_be16(is);
    h.sample_rate = read_be32(is);
    h.num_samples = read_be32(is);
    h.loop_start = read_be32(is);
    h.loop_end = read_be32(is);
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

// Decode AST file (supports both PCM and AFC ADPCM)
static inline AstHeader ast_decode(const std::string& path, std::vector<int16_t>& pcm_interleaved) {
    std::ifstream fs(path, std::ios::binary);
    if (!fs)
        throw std::runtime_error("Cannot open file: " + path);

    AstHeader hdr = ast_read_header(fs);
    pcm_interleaved.clear();

    if (hdr.codec_id == AstCodecId::PCM_S16BE_PLANAR) {
        size_t sample_bytes = hdr.bit_depth / 8;

        while (fs && !fs.eof()) {
            char chunk_hdr[8];
            fs.read(chunk_hdr, 8);
            if (fs.gcount() < 8) break;

            uint32_t chunk_type;
            std::memcpy(&chunk_type, chunk_hdr, 4);
            uint32_t block_size = bswap32(*reinterpret_cast<const uint32_t*>(chunk_hdr + 4));

            skip_bytes(fs, 24);

            if (chunk_type == 0x4B434C42) { // "BLCK" in little-endian
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
            }
            else {
                skip_bytes(fs, static_cast<size_t>(block_size) * hdr.channels);
            }
        }
    }
    else if (hdr.codec_id == AstCodecId::ADPCM_AFC) {
        std::vector<std::vector<int16_t>> all_pcm(hdr.channels);
        std::vector<int> hist(hdr.channels * 2, 0);

        while (fs && !fs.eof()) {
            char chunk_hdr[8];
            fs.read(chunk_hdr, 8);
            if (fs.gcount() < 8) break;

            uint32_t chunk_type;
            std::memcpy(&chunk_type, chunk_hdr, 4);
            uint32_t block_size = bswap32(*reinterpret_cast<const uint32_t*>(chunk_hdr + 4));

            skip_bytes(fs, 24);

            if (chunk_type == 0x4B434C42) { // "BLCK"
                for (size_t c = 0; c < hdr.channels; c++) {
                    std::vector<uint8_t> compressed_data(block_size);
                    read_bytes(fs, compressed_data.data(), block_size);

                    int h1 = hist[c * 2];
                    int h2 = hist[c * 2 + 1];
                    std::vector<int16_t> decoded = decode_afc_block(compressed_data.data(), block_size, h1, h2);
                    all_pcm[c].insert(all_pcm[c].end(), decoded.begin(), decoded.end());
                    hist[c * 2] = h1;
                    hist[c * 2 + 1] = h2;
                }
            }
            else {
                skip_bytes(fs, static_cast<size_t>(block_size) * hdr.channels);
            }
        }

        size_t total_frames = all_pcm[0].size();
        pcm_interleaved.resize(total_frames * hdr.channels);
        for (size_t i = 0; i < total_frames; i++) {
            for (size_t c = 0; c < hdr.channels; c++) {
                pcm_interleaved[i * hdr.channels + c] = all_pcm[c][i];
            }
        }
    }
    else {
        throw std::runtime_error("Unsupported codec: " + std::to_string(static_cast<int>(hdr.codec_id)));
    }

    return hdr;
}

// Encode AST file with PCM (lossless)
static inline void ast_encode_pcm(const std::string& path,
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
    }
    else {
        write_be32(fs, 0);
    }

    if (le > 0 && ls >= 0) {
        write_be32(fs, static_cast<uint32_t>(le));
    }
    else {
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

// Encode AST file with AFC ADPCM (compressed)
static inline void ast_encode_afc(const std::string& path,
    const int16_t* pcm_interleaved,
    size_t num_frames,
    uint16_t channels,
    uint32_t sample_rate,
    int64_t loop_start_ms = -1,
    int64_t loop_end_ms = 0) {
    std::ofstream fs(path, std::ios::binary);
    if (!fs)
        throw std::runtime_error("Cannot create file: " + path);

    ast_write_header(fs, AstCodecId::ADPCM_AFC, channels, sample_rate,
        loop_start_ms, loop_end_ms);

    constexpr size_t kBlockFrames = 1024;
    size_t written_frames = 0;
    uint32_t first_block_size = 0;
    uint32_t frame_count = 0;

    std::vector<std::vector<int16_t>> ch_pcm(channels);
    for (size_t c = 0; c < channels; c++) {
        ch_pcm[c].resize(num_frames);
        for (size_t i = 0; i < num_frames; i++) {
            ch_pcm[c][i] = pcm_interleaved[i * channels + c];
        }
    }

    std::vector<int> hist(channels * 2, 0);

    while (written_frames < num_frames) {
        size_t cur = kBlockFrames;
        if (cur > num_frames - written_frames)
            cur = num_frames - written_frames;

        write_fourcc(fs, "BLCK");

        std::vector<std::vector<uint8_t>> enc_data(channels);
        for (size_t c = 0; c < channels; c++) {
            int h1 = hist[c * 2];
            int h2 = hist[c * 2 + 1];
            enc_data[c] = encode_afc_block(ch_pcm[c].data() + written_frames, (int)cur, h1, h2);
            hist[c * 2] = h1;
            hist[c * 2 + 1] = h2;
        }

        uint32_t block_size = (uint32_t)enc_data[0].size();
        write_be32(fs, block_size);
        write_zeros(fs, 24);

        for (size_t c = 0; c < channels; c++) {
            write_bytes(fs, enc_data[c].data(), enc_data[c].size());
        }

        if (frame_count == 0)
            first_block_size = block_size;

        written_frames += cur;
        ++frame_count;
    }

    int64_t file_size = fs.tellp();

    fs.seekp(20, std::ios::beg);
    write_be32(fs, static_cast<uint32_t>(num_frames));
    write_be32(fs, 0);
    write_be32(fs, static_cast<uint32_t>(num_frames));
    write_be32(fs, first_block_size);

    fs.seekp(4, std::ios::beg);
    write_be32(fs, static_cast<uint32_t>(file_size - 64));
}

// Main encoding function with codec selection
static inline void ast_encode(const std::string& path,
    const int16_t* pcm_interleaved,
    size_t num_frames,
    uint16_t channels,
    uint32_t sample_rate,
    AstCodecId codec_id = AstCodecId::PCM_S16BE_PLANAR,
    int64_t loop_start_ms = -1,
    int64_t loop_end_ms = 0) {
    if (codec_id == AstCodecId::PCM_S16BE_PLANAR) {
        ast_encode_pcm(path, pcm_interleaved, num_frames, channels, sample_rate, loop_start_ms, loop_end_ms);
    }
    else if (codec_id == AstCodecId::ADPCM_AFC) {
        ast_encode_afc(path, pcm_interleaved, num_frames, channels, sample_rate, loop_start_ms, loop_end_ms);
    }
    else {
        throw std::runtime_error("Unsupported codec for encoding");
    }
}