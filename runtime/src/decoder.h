#pragma once

#include "rt_types.h" // data_buffer_t

namespace hle_audio {
namespace rt {

struct decoder_ti {
    size_t (*release_consumed_inputs)(void* state);
    bool (*queue_input)(void* state, const data_buffer_t& buf, bool last_input);
    data_buffer_t (*next_output)(void* state, const data_buffer_t& current_buf);
    bool (*is_running)(void* state);

    /**
     * @brief reset inputs and outputs
     */
    void (*flush)(void* state);

    // destructor, todo: move out lifecycle management out of api
    void (*destroy)(void* state);
};

struct decoder_t {
    const decoder_ti* vt;
    void* state;
};

static size_t release_consumed_inputs(decoder_t& dec) {
    return dec.vt->release_consumed_inputs(dec.state);
}

static bool queue_input(decoder_t& dec, const data_buffer_t& buf, bool last_input = false) {
    return dec.vt->queue_input(dec.state, buf, last_input);
}

static data_buffer_t next_output(decoder_t& dec, const data_buffer_t& current_buf) {
    return dec.vt->next_output(dec.state, current_buf);
}

static bool is_running(decoder_t& dec) {
    return dec.vt->is_running(dec.state);
}

static void flush(decoder_t& dec) {
    assert(dec.vt->flush);
    dec.vt->flush(dec.state);
}

static void destroy(decoder_t& dec) {
    assert(dec.vt->destroy);
    dec.vt->destroy(dec.state);
}

}
}
