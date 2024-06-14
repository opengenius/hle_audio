#pragma once

#include "data_types.h"

namespace hle_audio {
namespace data {

class file_data_provider_t : public hle_audio::data::audio_file_data_provider_ti {
public:
    const char* sounds_path;
    bool use_oggs = false;

    hle_audio::data::audio_file_data_t get_file_data(const char* filename, uint32_t file_index) override;
};

}
}
