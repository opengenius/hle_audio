#pragma once

#include "data_types.h"
#include "rapidjson/document.h"

namespace hle_audio {
namespace data {

bool migrate_from_v1(data_state_t* state, rapidjson::Document& document);

}
}
