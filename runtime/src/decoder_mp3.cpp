#include "decoder_mp3.h"

// #define MINIMP3_ONLY_MP3
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "rt_types.h"
#include "hlea/runtime.h"

#include <atomic>
#include <cassert>
#include <cstdio>

#include "jobs_utils.inl"
#include "alloc_utils.inl"

namespace hle_audio {
namespace rt {

static const size_t MAX_INPUT_BUFFERS = 2;
static const size_t MAX_OUTPUT_BUFFERS = 4;

struct output_buffer_t {
    float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int frame_count;
    int channels;
};

template<typename T, size_t ring_indices_range>
struct ring_indices {
    using indices_type = T;
    static const size_t range_size = ring_indices_range;

    std::atomic<indices_type> read_pos;
    std::atomic<indices_type> write_pos;

    bool can_write() {
        return write_pos.load() != indices_type(read_pos.load() + range_size);
    }
};

// ~10 mp3 frames
static const size_t MIN_DATA_CHUNK_SIZE = 16384;

struct mp3_decoder_t {
    // todo: consider moving these upper level refs to specific function context parameters
    allocator_t allocator;
    jobs_t jobs_sys;

    data_buffer_t inputs[MAX_INPUT_BUFFERS];
    uint8_t input_count;
    uint8_t consumed_input_count;

    struct job_state_t {
        mp3dec_t mp3d;

        data_buffer_t input;
        bool not_enough_input_data;

        uint8_t aux_input_buf[MIN_DATA_CHUNK_SIZE];
        size_t aux_input_size;
        data_buffer_t aux_input;

        output_buffer_t outputs[MAX_OUTPUT_BUFFERS];
        ring_indices<uint8_t, MAX_OUTPUT_BUFFERS> output_indices;
        std::atomic<bool> running;
    } job_state;
};

mp3_decoder_t* create_decoder(const mp3_decoder_create_info_t& info) {
    auto dec = allocate<mp3_decoder_t>(info.allocator);
    new(dec) mp3_decoder_t(); // init c++ stuff
    dec->allocator = info.allocator;
    dec->jobs_sys = info.jobs;

    mp3dec_init(&dec->job_state.mp3d);
    
    return dec;
}

void destroy(mp3_decoder_t* dec) {
    dec->~mp3_decoder_t();
    deallocate(dec->allocator, dec);
}

bool is_running(const mp3_decoder_t* dec) {
    return dec->job_state.running;
}

//---------------------------------------------------------------------------------------
// job

static void consume_rest_input(mp3_decoder_t::job_state_t* state) {
    // keep the rest part of input
    if (0 < state->input.size) {
        assert(state->input.size <= sizeof(state->aux_input_buf));

        memcpy(state->aux_input_buf, state->input.data, state->input.size);
        state->aux_input_size = state->input.size;
    }

    state->input = {};
    state->not_enough_input_data = true;
}

static void decode_mp3(mp3_decoder_t::job_state_t* state) {
    /*
        todo: check if input is less than MIN_DATA_CHUNK_SIZE;

        move to some temp buffer
        input: [---][----*------------]
            =>
        temp:  [---------]
                   min chunk size
        next time process input from *
    */

    assert(state->input.size);

    if (state->aux_input_size && !state->aux_input.data) {
        if (state->aux_input_size < MIN_DATA_CHUNK_SIZE) {
            auto input_moved_to_aux_size = MIN_DATA_CHUNK_SIZE - state->aux_input_size;
            input_moved_to_aux_size = input_moved_to_aux_size < state->input.size ? input_moved_to_aux_size : state->input.size;
            memcpy(&state->aux_input_buf[state->aux_input_size], state->input.data, input_moved_to_aux_size);

            data_buffer_t aux_input = {};
            aux_input.data = state->aux_input_buf;
            aux_input.size = state->aux_input_size + input_moved_to_aux_size;
            state->aux_input = aux_input;
        }
    }
    
    while(state->output_indices.can_write()) {

        if (is_empty(state->input)) {
            consume_rest_input(state);
            break;
        }

        auto input_ptr = state->aux_input.data ? &state->aux_input : &state->input;

        auto wp = state->output_indices.write_pos.load();
        auto& output = state->outputs[wp & (MAX_OUTPUT_BUFFERS - 1)];

        mp3dec_frame_info_t info;
        output.frame_count = mp3dec_decode_frame(&state->mp3d, input_ptr->data, input_ptr->size, output.pcm, &info);
        output.channels = info.channels;

        // Insufficient data
        assert(output.frame_count);

        *input_ptr = advance(*input_ptr, info.frame_bytes);
        if (state->aux_input.data) {
            auto aux_processed_bytes = state->aux_input.data - state->aux_input_buf;
            // use current input when processed aux buffer from previous input
            if (state->aux_input_size <= aux_processed_bytes) {
                // advance input with bytes 
                state->input = advance(state->input, aux_processed_bytes - state->aux_input_size);

                // reset aux buffer
                state->aux_input_size = 0;
                state->aux_input = {};
            }
        }

        state->output_indices.write_pos.store(++wp);

        // consume input if less than 5 mp3 frames data left
        if (state->input.size < MIN_DATA_CHUNK_SIZE / 2) {
            consume_rest_input(state);
            break;
        }
    }

    // we could've been here when out_read_pos is increased from the poll thread (output_indices.read_pos.store++)
    state->running = false;
}

static void decode_mp3_jobfunc(void* udata) {
    auto state = (mp3_decoder_t::job_state_t*)udata;
    decode_mp3(state);
}

// job
//---------------------------------------------------------------------------------------

static size_t get_frames_byte_size(const output_buffer_t& output) {
    return sizeof(float) * output.frame_count * output.channels;
}

static size_t release_consumed_inputs(mp3_decoder_t* dec) {
    if (!dec->job_state.running) {
        if (dec->job_state.not_enough_input_data) {
            dec->job_state.not_enough_input_data = false;

            ++dec->consumed_input_count;
            assert(dec->input_count);
        }
    }

    // todo: always 1 in this case?
    auto consumed_input_count = dec->consumed_input_count;

    if (consumed_input_count) {
        memmove(dec->inputs, &dec->inputs[consumed_input_count], (dec->input_count - consumed_input_count) * sizeof(dec->inputs[0]));
        dec->input_count -= consumed_input_count;
        dec->consumed_input_count = 0;        
    }

    return consumed_input_count;
}

static void kick_decoding_job(mp3_decoder_t* dec) {
    if (dec->job_state.running) return;

    if (dec->job_state.not_enough_input_data) {
        // wait till release_consumed_inputs
        // todo: ?consume here?
        return;
    }

    // do not launch if has no input
    bool has_inputs = dec->consumed_input_count < dec->input_count;
    if (!has_inputs) return;

    // or outputs
    if (!dec->job_state.output_indices.can_write()) return;

    if (dec->job_state.input.data == nullptr) {
        dec->job_state.input = dec->inputs[dec->consumed_input_count];
    }

    assert(dec->job_state.input.size);

    // launch decoder job
    dec->job_state.running = true;

    hlea_job_t job  = {};
    job.job_func = decode_mp3_jobfunc;
    job.udata = &dec->job_state;
    launch(dec->jobs_sys, job);
}

static bool queue_input(mp3_decoder_t* state, const data_buffer_t& buf) {
    // should not be the case ever, ?assert?
    if (state->input_count == MAX_INPUT_BUFFERS) return false;

    state->inputs[state->input_count++] = buf;

    // launch job 
    kick_decoding_job(state);

    return true;
}

static data_buffer_t next_output(mp3_decoder_t* dec, const data_buffer_t& current_buf) {
    auto rp = dec->job_state.output_indices.read_pos.load();

    // release previous buffer
    if (current_buf.size) {
        assert(dec->job_state.outputs[(rp & MAX_OUTPUT_BUFFERS - 1)].pcm == (float*)current_buf.data);
        
        dec->job_state.output_indices.read_pos.store(++rp);

        // we now have one vacant output buffer to decode into
        // todo: here it is possible to get into condition where job is almost finished, but running flag is still true
        // launch decoding job
        kick_decoding_job(dec);
    }
    
    // return empty if next is still writing
    auto wp = dec->job_state.output_indices.write_pos.load();
    if (rp == wp) {
        return {};
    }

    uint8_t read_buf_index = rp & (MAX_OUTPUT_BUFFERS - 1);

    data_buffer_t res = {};
    res.data = (uint8_t*)dec->job_state.outputs[read_buf_index].pcm;
    res.size = get_frames_byte_size(dec->job_state.outputs[read_buf_index]);
    return res;
}

//
// decoder_ti vtable
//

static size_t mp3dec_release_consumed_inputs(void* state) {
    auto dec = (mp3_decoder_t*)state;
    return release_consumed_inputs(dec);
}

static bool mp3dec_queue_input(void* state, const data_buffer_t& buf) {
    auto dec = (mp3_decoder_t*)state;
    return queue_input(dec, buf);
}

static data_buffer_t mp3dec_next_output(void* state, const data_buffer_t& current_buf) {
    auto dec = (mp3_decoder_t*)state;
    return next_output(dec, current_buf);
}

static const decoder_ti g_mp3_decoder_tv = {
    mp3dec_release_consumed_inputs,
    mp3dec_queue_input,
    mp3dec_next_output
};

decoder_t cast_to_decoder(mp3_decoder_t* dec) {
    decoder_t res = {};
    res.vt = &g_mp3_decoder_tv;
    res.state = dec;
    return res;
}

}
}
