#include "editor/editor_runtime.h"
#include "internal_editor_runtime.h"
#include "internal_types.h"

#include "miniaudio_public.h"

#include <unordered_map>
#include <string>
#include "file_utils.inl"

namespace hle_audio {
namespace rt {

struct runtime_env_t {
    ma_vfs* pVFS;
    allocator_t allocator;
    async_file_reader_t* async_io;
    chunk_streaming_cache_t* cache;
    ma_engine* engine;
};

struct editor_runtime_t {
    runtime_env_t env;

    struct {
        data_buffer_t buffer;
        ma_decoder decoder;
        ma_sound sound;
    } file_player;
    bool file_sound_inited;
    const char* sounds_path; // file path prefix to use in file loading

    struct cache_record_t {
        uint32_t use_count;

        ma_vfs_file file;
        async_file_handle_t afile;
        bank_streaming_source_info_t streaming_info;
    };
    std::unordered_map<uint32_t, cache_record_t> streaming_file_cache;

    struct file_data_record_t {
        std::string file_path;
        rt::range_t data_chunk_range;
    };
    std::vector<file_data_record_t> file_data_cache;
};

editor_runtime_t* create_editor_runtime() {
    return new editor_runtime_t();
}

void destroy(editor_runtime_t* rt) {
    drop_file_cache(rt);
    
    delete rt;
}

void bind(editor_runtime_t* editor_rt, hlea_context_t* ctx) {
    editor_api_t hooks = {};
    hooks.inst = editor_rt;
    hooks.retrieve_streaming_info = hle_audio::rt::retrieve_streaming_info;
    ctx->editor_hooks = hooks;

    hle_audio::rt::runtime_env_t env = {};
    env.pVFS = ctx->pVFS;
    env.allocator = ctx->allocator;
    env.async_io = ctx->async_io;
    env.cache = ctx->streaming_cache;
    env.engine = &ctx->engine;
    
    editor_rt->env = env;
}

void cache_audio_file_data(editor_runtime_t* rt, const char* path, uint32_t file_index, rt::range_t data_chunk_range) {
    if (rt->file_data_cache.size() <= file_index) {
        rt->file_data_cache.resize(file_index + 1);
    }

    editor_runtime_t::file_data_record_t fd_rec = {};
    fd_rec.file_path = path;
    fd_rec.data_chunk_range = data_chunk_range;
    rt->file_data_cache[file_index] = fd_rec;
}

void drop_file_cache(editor_runtime_t* rt) {
    for (auto it : rt->streaming_file_cache) {
        deregister_source(rt->env.cache, it.second.streaming_info.streaming_src);
        stop_async_reading(rt->env.async_io, it.second.afile);
        ma_vfs_close(rt->env.pVFS, it.second.file);
    }
    rt->streaming_file_cache.clear();
    rt->file_data_cache.clear();
}

bank_streaming_source_info_t retrieve_streaming_info(editor_runtime_t* editor_rt, uint32_t file_index) {
    // try opened
    auto it = editor_rt->streaming_file_cache.find(file_index);
    if (it != editor_rt->streaming_file_cache.end()) {
        ++it->second.use_count;
        return it->second.streaming_info;
    }

    auto& fd_rec = editor_rt->file_data_cache[file_index];

    const char* path = fd_rec.file_path.c_str();

    char path_buf[512];
    if (editor_rt->sounds_path) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", editor_rt->sounds_path, path);
        path = path_buf;
    }

    auto vfs = editor_rt->env.pVFS;

    // open streaming file
    ma_vfs_file file;
    auto result = ma_vfs_open(vfs, path, MA_OPEN_MODE_READ, &file);
    if (result == MA_SUCCESS) {
        auto afile = start_async_reading(editor_rt->env.async_io, file);
        // todo: handle error
        assert(afile);

        auto str_src = register_source(editor_rt->env.cache, afile);
        // todo: handle error
        assert(str_src);

        bank_streaming_source_info_t res = {};
        res.streaming_src = str_src;
        res.file_range = fd_rec.data_chunk_range;

        editor_runtime_t::cache_record_t cache_file = {};
        cache_file.use_count = 1;
        cache_file.file = file;
        cache_file.afile = afile;
        cache_file.streaming_info = res;

        editor_rt->streaming_file_cache[file_index] = cache_file;

        return res;

        // todo: enable on error handling
        // ma_vfs_close(vfs, file);
    }

    return {};
}

void set_sounds_path(editor_runtime_t* editor_rt, const char* sounds_path) {
    editor_rt->sounds_path = sounds_path;
}

void play_file(editor_runtime_t* editor_rt, const char* file_path) {
    stop_file(editor_rt);

    auto& player_ref = editor_rt->file_player;

    ma_result result = read_file(editor_rt->env.pVFS, file_path, editor_rt->env.allocator, &player_ref.buffer);

    ma_decoder_config decoder_config = ma_decoder_config_init(
        ma_format_f32, 0, 
        ma_engine_get_sample_rate(editor_rt->env.engine));
    result = ma_decoder_init_memory(player_ref.buffer.data, player_ref.buffer.size, &decoder_config, &player_ref.decoder);
    if (result != MA_SUCCESS) {
        return;
    }
    result = ma_sound_init_from_data_source(editor_rt->env.engine, 
            &player_ref.decoder,
            0, NULL, 
            &player_ref.sound);

    if (result != MA_SUCCESS) {
        return;
    }
    editor_rt->file_sound_inited = true;

    ma_sound_start(&player_ref.sound);
}

bool is_file_playing(editor_runtime_t* editor_rt) {
    if (editor_rt->file_sound_inited) {
        return ma_sound_is_playing(&editor_rt->file_player.sound);
    }

    return false;
}

void stop_file(editor_runtime_t* editor_rt) {
    if (editor_rt->file_sound_inited) {
        auto& player_ref = editor_rt->file_player;

        ma_sound_stop(&player_ref.sound);
        ma_sound_uninit(&player_ref.sound);

        ma_decoder_uninit(&player_ref.decoder);

        deallocate(editor_rt->env.allocator, player_ref.buffer.data);
        player_ref.buffer = {};

        editor_rt->file_sound_inited = false;
    }
}

}
}
