#pragma once

#include "data_types.h"
#include "rapidjson/document.h"

namespace hle_audio {
namespace data {

bool migrate_to_v3(data_state_t* state, rapidjson::Document& document);

}
}
