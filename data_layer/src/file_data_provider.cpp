#include "file_data_provider.h"

#include <span>
#include <format>
#include <filesystem>
#include <fstream>

using hle_audio::editor::audio_file_data_t;

static uint32_t scan_uint32(const uint8_t** data_ptr) {
    uint32_t value;
    memcpy(&value, *data_ptr, sizeof(value));
    *data_ptr += sizeof(value);
    return value;
}

/**
 * Extracts loop info from smpl subchunk of WAVE riff file
 */
static bool find_wav_loop_point(const std::span<const uint8_t>& buffer_data, uint64_t* out_loop_start, uint64_t* out_loop_end) {
    union tag_t {
        char ansi[4];
        uint32_t single;
    };

    const tag_t RIFF_tag = {'R', 'I', 'F', 'F'};
    const tag_t WAVE_tag = {'W', 'A', 'V', 'E'};
    const tag_t smpl_tag = {'s', 'm', 'p', 'l'};

    auto data_ptr = buffer_data.data();

    uint32_t riff_id = scan_uint32(&data_ptr);
    if (riff_id != RIFF_tag.single) return false;

    // move to format
    data_ptr += sizeof(uint32_t);

    uint32_t fortat_tag = scan_uint32(&data_ptr);
    if (fortat_tag != WAVE_tag.single) return false;

    // chunks
    auto buffer_end_ptr = buffer_data.data() + buffer_data.size();
    while(data_ptr < buffer_end_ptr) {
        uint32_t chunk_tag = scan_uint32(&data_ptr);
        uint32_t chunk_size = scan_uint32(&data_ptr);

        if (chunk_tag == smpl_tag.single) {
            uint32_t loop_count = 0;
            memcpy(&loop_count, data_ptr + 28, sizeof(loop_count));

            if (loop_count) {
                uint32_t loop_start, loop_end;
                memcpy(&loop_start, data_ptr + 44, sizeof(loop_start));
                memcpy(&loop_end, data_ptr + 48, sizeof(loop_end));

                *out_loop_start = loop_start;
                *out_loop_end = loop_end;
                return true;
            }

            // smpl chunk found, no need to look more
            break;
        }
        
        data_ptr += chunk_size;
    }

    return false;
}

namespace hle_audio {
namespace data {

audio_file_data_t file_data_provider_t::get_file_data(const char* filename, uint32_t file_index) {
    std::string full_path = std::format("{}/{}", _sounds_path, filename);

    auto fp = fopen(full_path.c_str(), "rb");

    if (!fp) return {};

    // get file size
    fseek(fp, 0L, SEEK_END);
    auto file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    std::vector<uint8_t> file_buf(file_size);

    // read file data
    fread(file_buf.data(), 1, file_size, fp);
    fclose(fp);

    audio_file_data_t res = {};
    res.content = std::move(file_buf);

    uint64_t loop_start, loop_end;
    if (find_wav_loop_point(res.content, &loop_start, &loop_end)) {
        res.meta.loop_start = loop_start;
        res.meta.loop_end = loop_end;
    }

    if (use_oggs) {
        namespace fs = std::filesystem;
        fs::path ogg_path = fs::path(_sounds_path) / "oggs" / filename;
        ogg_path.replace_extension("ogg");
        if (fs::exists(ogg_path)) {
            auto fsize = fs::file_size(ogg_path);

            std::vector<uint8_t> file_buf(fsize);
            std::ifstream is(ogg_path, std::ios::binary);
            is.read((char*)file_buf.data(), file_buf.size());

            res.content = std::move(file_buf);
        }
    }

    return res;
}

}
}
