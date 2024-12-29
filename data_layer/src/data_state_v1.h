#pragma once

#include "data_types.h"
#include "rapidjson/document.h"

namespace hle_audio {
namespace data {

namespace v2 {
    struct data_state_t;
}

bool migrate_from_v1_to_v2(v2::data_state_t* state, rapidjson::Document& document);

}
}
