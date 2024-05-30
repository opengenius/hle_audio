#pragma once

#include "internal_types.h"

namespace hle_audio {
namespace rt {

struct editor_runtime_t;

bank_streaming_source_info_t retrieve_streaming_info(editor_runtime_t* editor_rt, uint32_t file_index);

}
}
