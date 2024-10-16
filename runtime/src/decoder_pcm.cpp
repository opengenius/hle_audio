#include "decoder_pcm.h"

#include "alloc_utils.inl"

namespace hle_audio {
namespace rt {

/**
 * @brief pcm decoder
 * todo:
 *  - support streaming data source (multiple inputs)
 */

static const size_t MAX_INPUT_BUFFERS = 2;

struct pcm_decoder_t {
    allocator_t allocator;

    data_buffer_t inputs[MAX_INPUT_BUFFERS];
    uint8_t input_count;
    uint8_t consumed_input_count;
};

using hle_audio::rt::data_buffer_t;

static const data_buffer_t empty_data_buffer = {};

pcm_decoder_t* create_decoder(const pcm_decoder_create_info_t& info) {
    auto dec = allocate<pcm_decoder_t>(info.allocator);
    *dec = {};
    dec->allocator = info.allocator;
    return dec;
}

void destroy(pcm_decoder_t* dec) {
    deallocate(dec->allocator, dec);
}

void reset(pcm_decoder_t* dec) {
    dec->input_count = 0;
    dec->consumed_input_count = 0;
}

static size_t release_consumed_inputs(pcm_decoder_t* dec) {
    auto consumed_input_count = dec->consumed_input_count;

    if (consumed_input_count) {
        for (auto i = consumed_input_count; i < dec->input_count; ++i) {
            dec->inputs[i - consumed_input_count] = dec->inputs[i];
        }
        dec->input_count -= consumed_input_count;
        dec->consumed_input_count = 0;        
    }

    return consumed_input_count;
}

static bool queue_input(pcm_decoder_t* dec, const data_buffer_t& buf, bool last_input) {
    // should not be the case ever, ?assert?
    if (dec->input_count == MAX_INPUT_BUFFERS) return false;

    dec->inputs[dec->input_count++] = buf;

    return true;
}

static void release_output(pcm_decoder_t* dec, data_buffer_t output_buf) {
    if (!output_buf.size) return;

    assert(dec->input_count);
    assert(dec->inputs[dec->consumed_input_count].data == output_buf.data);
    assert(dec->inputs[dec->consumed_input_count].size == output_buf.size);

    ++dec->consumed_input_count;
}

static data_buffer_t next_output(pcm_decoder_t* dec, const data_buffer_t& output_buf) {
    // release previous buffer
    release_output(dec, output_buf);

    return dec->consumed_input_count < dec->input_count ? dec->inputs[dec->consumed_input_count] : empty_data_buffer;
}

static void flush(pcm_decoder_t* dec) {
    dec->consumed_input_count = 0;
    dec->input_count = 0;
}


//
// decoder_ti vtable
//

static size_t pcm_dec_release_consumed_inputs(void* state) {
    auto dec = (pcm_decoder_t*)state;
    return release_consumed_inputs(dec);
}

static bool pcm_dec_queue_input(void* state, const data_buffer_t& buf, bool last_input) {
    auto dec = (pcm_decoder_t*)state;
    return queue_input(dec, buf, last_input);
}

static data_buffer_t pcm_dec_next_output(void* state, const data_buffer_t& current_buf) {
    auto dec = (pcm_decoder_t*)state;
    return next_output(dec, current_buf);
}

static bool pcm_dec_is_running(void* state) {
    // no async tasks for pcm
    return false;
}

static void pcm_dec_flush(void* state) {
    auto dec = (pcm_decoder_t*)state;
    flush(dec);
}

static const decoder_ti g_wav_decoder_vt = []() {
    decoder_ti vt = {};
    vt.release_consumed_inputs = pcm_dec_release_consumed_inputs;
    vt.queue_input = pcm_dec_queue_input;
    vt.next_output = pcm_dec_next_output;
    vt.is_running = pcm_dec_is_running;
    vt.flush = pcm_dec_flush;

    return vt;
}();

decoder_t cast_to_decoder(pcm_decoder_t* dec) {
    decoder_t res = {};
    res.vt = &g_wav_decoder_vt;
    res.state = dec;
    return res;
}

}
}
