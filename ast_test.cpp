#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include "ast_codec.h"

struct WavInfo {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    size_t   num_frames;
};

static WavInfo wav_read(const std::string& path, std::vector<int16_t>& pcm) {
    std::ifstream fs(path, std::ios::binary);
    if (!fs)
        throw std::runtime_error("Cannot open WAV: " + path);

    char riff[4];
    fs.read(riff, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0)
        throw std::runtime_error("Not a WAV file: missing RIFF header");

    read_le32(fs);

    char wave[4];
    fs.read(wave, 4);
    if (std::memcmp(wave, "WAVE", 4) != 0)
        throw std::runtime_error("Not a WAV file: missing WAVE marker");

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    bool fmt_found = false;
    bool data_found = false;

    while (fs && !fs.eof()) {
        char chunk_id[4];
        fs.read(chunk_id, 4);
        if (fs.gcount() < 4) break;

        uint32_t chunk_size = read_le32(fs);

        if (std::memcmp(chunk_id, "fmt ", 4) == 0) {
            audio_format = static_cast<uint16_t>(read_le16(fs));
            channels = static_cast<uint16_t>(read_le16(fs));
            sample_rate = read_le32(fs);
            read_le32(fs);
            read_le16(fs);
            bits_per_sample = static_cast<uint16_t>(read_le16(fs));
            if (chunk_size > 16)
                skip_bytes(fs, chunk_size - 16);
            fmt_found = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            if (!fmt_found)
                throw std::runtime_error("WAV: data chunk before fmt chunk");
            if (audio_format != 1)
                throw std::runtime_error("Only PCM format (1) is supported, got format " + std::to_string(audio_format));
            if (bits_per_sample != 16)
                throw std::runtime_error("Only 16-bit WAV is supported, got " + std::to_string(bits_per_sample));

            size_t total_bytes = chunk_size;
            size_t total_samples = total_bytes / sizeof(int16_t);
            pcm.resize(total_samples);
            read_bytes(fs, pcm.data(), total_bytes);
            data_found = true;
            break;
        } else {
            skip_bytes(fs, chunk_size);
        }
    }

    if (!fmt_found || !data_found)
        throw std::runtime_error("WAV: missing fmt or data chunk");

    WavInfo info{};
    info.channels = channels;
    info.sample_rate = sample_rate;
    info.bits_per_sample = bits_per_sample;
    info.num_frames = pcm.size() / channels;
    return info;
}

static void wav_write(const std::string& path, const int16_t* pcm,
                       size_t num_frames, uint16_t channels, uint32_t sample_rate) {
    std::ofstream fs(path, std::ios::binary);
    if (!fs)
        throw std::runtime_error("Cannot create WAV: " + path);

    uint32_t data_size = static_cast<uint32_t>(num_frames * channels * sizeof(int16_t));
    uint32_t byte_rate = sample_rate * channels * sizeof(int16_t);
    uint16_t block_align = channels * sizeof(int16_t);

    fs.write("RIFF", 4);
    write_le32(fs, 36 + data_size);
    fs.write("WAVE", 4);

    fs.write("fmt ", 4);
    write_le32(fs, 16);
    write_le16(fs, 1);
    write_le16(fs, channels);
    write_le32(fs, sample_rate);
    write_le32(fs, byte_rate);
    write_le16(fs, block_align);
    write_le16(fs, 16);

    fs.write("data", 4);
    write_le32(fs, data_size);
    fs.write(reinterpret_cast<const char*>(pcm), data_size);
}

static std::string replace_ext(const std::string& path, const std::string& new_ext) {
    auto pos = path.rfind('.');
    if (pos != std::string::npos)
        return path.substr(0, pos) + new_ext;
    return path + new_ext;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  " << argv[0] << " -e input.wav    (encode WAV -> AST)" << std::endl;
        std::cerr << "  " << argv[0] << " -d input.ast    (decode AST -> WAV)" << std::endl;
        return 1;
    }

    std::string flag = argv[1];
    std::string input = argv[2];

    try {
        if (flag == "-e") {
            std::string output = replace_ext(input, ".ast");
            std::vector<int16_t> pcm;
            WavInfo info = wav_read(input, pcm);
            std::cout << "Input:  " << input << " (" << info.channels << "ch, "
                      << info.sample_rate << "Hz, " << info.num_frames << " frames)" << std::endl;
            ast_encode(output.c_str(), pcm.data(), info.num_frames,
                       info.channels, info.sample_rate);
            std::cout << "Output: " << output << std::endl;
        } else if (flag == "-d") {
            std::string output = replace_ext(input, ".wav");
            std::vector<int16_t> pcm;
            AstHeader hdr = ast_decode(input, pcm);
            size_t num_frames = pcm.size() / hdr.channels;
            std::cout << "Input:  " << input << " (" << hdr.channels << "ch, "
                      << hdr.sample_rate << "Hz, " << num_frames << " frames)" << std::endl;
            wav_write(output, pcm.data(), num_frames, hdr.channels, hdr.sample_rate);
            std::cout << "Output: " << output << std::endl;
        } else {
            std::cerr << "Unknown flag: " << flag << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
