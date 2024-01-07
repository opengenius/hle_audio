#pragma once

#include "rt_types.h" // data_buffer_t

namespace hle_audio {
namespace rt {

struct decoder_ti {   
    size_t (*release_consumed_inputs)(void* state);
    bool (*queue_input)(void* state, const data_buffer_t& buf);
    data_buffer_t (*next_output)(void* state, const data_buffer_t& current_buf);
    bool (*is_running)(void* state);
};

struct decoder_t {
    const decoder_ti* vt;
    void* state;
};

static size_t release_consumed_inputs(decoder_t& dec) {
    return dec.vt->release_consumed_inputs(dec.state);
}

static bool queue_input(decoder_t& dec, const data_buffer_t& buf) {
    return dec.vt->queue_input(dec.state, buf);
}

static data_buffer_t next_output(decoder_t& dec, const data_buffer_t& current_buf) {
    return dec.vt->next_output(dec.state, current_buf);
}

static bool is_running(decoder_t& dec) {
    return dec.vt->is_running(dec.state);
}


}
}
