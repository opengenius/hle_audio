#include "file_data_provider.h"

#include <format>
#include <filesystem>
#include <fstream>

#include "miniaudio_public.h"

using hle_audio::editor::audio_file_data_t;
using hle_audio::rt::const_data_buffer_t;

static uint32_t scan_uint32(const uint8_t** data_ptr) {
    uint32_t value;
    memcpy(&value, *data_ptr, sizeof(value));
    *data_ptr += sizeof(value);
    return value;
}

static const_data_buffer_t find_wav_chunk(const_data_buffer_t buffer_data, const char chunk_tag_ansi[4]) {
    union tag_t {
        char ansi[4];
        uint32_t single;
    };

    const tag_t RIFF_tag = {'R', 'I', 'F', 'F'};
    const tag_t WAVE_tag = {'W', 'A', 'V', 'E'};

    auto data_ptr = buffer_data.data;

    uint32_t riff_id = scan_uint32(&data_ptr);
    if (riff_id != RIFF_tag.single) return {};

    // move to format
    data_ptr += sizeof(uint32_t);

    uint32_t fortat_tag = scan_uint32(&data_ptr);
    if (fortat_tag != WAVE_tag.single) {
        return {};
    }

    tag_t required_tag = {};
    memcpy(required_tag.ansi, chunk_tag_ansi, sizeof(required_tag));

    // chunks
    auto buffer_end_ptr = buffer_data.data + buffer_data.size;
    while(data_ptr < buffer_end_ptr) {
        uint32_t chunk_tag = scan_uint32(&data_ptr);
        uint32_t chunk_size = scan_uint32(&data_ptr);

        if (chunk_tag == required_tag.single) {
            const_data_buffer_t res = {};
            res.data = data_ptr;
            res.size = chunk_size;
            return res;
        }
        
        data_ptr += chunk_size;
    }

    return {};
}

/**
 * Extracts loop info from smpl subchunk of WAVE riff file
 */
static bool find_wav_loop_point(const_data_buffer_t buffer_data, uint64_t* out_loop_start, uint64_t* out_loop_end) {    
    const char smpl_tag_ansi[4] = {'s', 'm', 'p', 'l'};
    auto data_range = find_wav_chunk(buffer_data, smpl_tag_ansi);
    auto data_ptr = data_range.data;
    if (data_ptr) {
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
    }

    return false;
}

namespace hle_audio {
namespace data {

namespace fs = std::filesystem;

static std::vector<uint8_t> read_file(const fs::path& file_path) {
    if (!fs::exists(file_path)) return {};

    auto fsize = fs::file_size(file_path);

    std::vector<uint8_t> file_buf(fsize);
    std::ifstream is(file_path, std::ios::binary);
    is.read((char*)file_buf.data(), file_buf.size());

    return file_buf;
}

audio_file_data_t file_data_provider_t::get_file_data(const char* filename, uint32_t file_index) {
    fs::path full_path = fs::path(sounds_path) / filename;
    std::vector<uint8_t> file_buf = read_file(full_path);
    if (file_buf.size() == 0) return {};

    const_data_buffer_t file_data_buf = {};
    file_data_buf.data = file_buf.data();
    file_data_buf.size = file_buf.size();

    rt::file_data_t::meta_t meta = {};

    // loop points are suppoted from wavs only (?do other format have something similar?)
    if (find_wav_loop_point(file_data_buf, &meta.loop_start, &meta.loop_end)) {
        // got wav loop range, nothing to do here        
    }

    if (full_path.extension() == ".wav") {
        meta.coding_format = rt::audio_format_type_e::pcm;
    } else if (full_path.extension() == ".mp3") {
        meta.coding_format = rt::audio_format_type_e::mp3;
    }

    audio_file_data_t res = {};
    res.content = std::move(file_buf);

    if (use_oggs) {
        fs::path ogg_path = fs::path(sounds_path) / "oggs" / filename;
        ogg_path.replace_extension("ogg");
        auto ogg_file_data = read_file(ogg_path);
        if (ogg_file_data.size()) {
            meta.coding_format = rt::audio_format_type_e::vorbis;
            res.content = std::move(ogg_file_data);
        }
    }

    // setup data chunk range
    rt::range_t range = {};
    range.size = res.content.size();

    file_data_buf.data = res.content.data();
    file_data_buf.size = res.content.size();
    const char data_tag_ansi[4] = {'d', 'a', 't', 'a'};
    auto data_range = find_wav_chunk(file_data_buf, data_tag_ansi);
    if (data_range.data) {
        range.offset = data_range.data - file_data_buf.data;
        range.size = data_range.size;
    }

    res.data_chunk_range = range;
    

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 48000);
    ma_decoder decoder;
    auto result = ma_decoder_init_memory(res.content.data(), res.content.size(), &config, &decoder);
    assert(result == MA_SUCCESS);

    ma_uint64 internalLengthInPCMFrames;
    result = ma_data_source_get_length_in_pcm_frames(decoder.pBackend, &internalLengthInPCMFrames);
    assert(result == MA_SUCCESS);
    meta.length_in_samples = internalLengthInPCMFrames;

    ma_uint32 internalChannels;
    ma_uint32 internalSampleRate;
    result = ma_data_source_get_data_format(decoder.pBackend, NULL, &internalChannels, &internalSampleRate, NULL, 0);
    assert(result == MA_SUCCESS);
    meta.channels = internalChannels;
    meta.sample_rate = internalSampleRate;

    ma_decoder_uninit(&decoder);

    res.meta = meta;

    assert(res.data_chunk_range.size);

    return res;
}

}
}
