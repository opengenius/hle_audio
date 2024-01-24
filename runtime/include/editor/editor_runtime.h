#pragma once

#include <cstdint>
#include "rt_types.h"

namespace hle_audio {
namespace rt {

struct editor_runtime_t;

editor_runtime_t* create_editor_runtime();
void destroy(editor_runtime_t* rt);

void cache_audio_file_data(editor_runtime_t* rt, const char* path, uint32_t file_index, rt::range_t data_chunk_range);
void drop_file_cache(editor_runtime_t* rt);

void set_sounds_path(editor_runtime_t* editor_rt, const char* sounds_path);
void play_file(editor_runtime_t* editor_rt, const char* file_path);
bool is_file_playing(editor_runtime_t* editor_rt);
void stop_file(editor_runtime_t* editor_rt);

}
}
