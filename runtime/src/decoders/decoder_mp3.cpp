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
    int frame_offset;
    int frame_count;
    int channels;
};

struct input_buffer_t {
    data_buffer_t buffer;
    bool last;
};

template<typename T, size_t ring_indices_range>
struct ring_indices {
    using indices_type = T;
    static const size_t range_size = ring_indices_range;

    std::atomic<indices_type> read_pos;
    std::atomic<indices_type> write_pos;

    void reset() {
        read_pos = write_pos = {};
    }

    bool can_write() const {
        return write_pos.load() != indices_type(read_pos.load() + range_size);
    }
};

// ~10 mp3 frames
static const size_t MIN_DATA_CHUNK_SIZE = 16384;

struct mp3_decoder_t {
    // todo: consider moving these upper level refs to specific function context parameters
    allocator_t allocator;
    jobs_t jobs_sys;

    input_buffer_t inputs[MAX_INPUT_BUFFERS];
    uint8_t input_count;
    uint8_t consumed_input_count;

    bool reset_state;

    struct job_state_t {
        mp3dec_t mp3d;

        input_buffer_t input;
        bool not_enough_input_data;

        uint8_t aux_input_buf[MIN_DATA_CHUNK_SIZE];
        size_t aux_input_size;
        data_buffer_t aux_input;

        output_buffer_t outputs[MAX_OUTPUT_BUFFERS];
        ring_indices<uint8_t, MAX_OUTPUT_BUFFERS> output_indices;
        std::atomic<bool> running;
        std::atomic<bool> stop_requested;
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

void reset(mp3_decoder_t* dec) {
    auto alloc = dec->allocator;
    auto jobs = dec->jobs_sys;

    // destroy + create witout allocation
    // todo: fix duplication | use destroy -> create_decoder on upper level, too heavy?
    dec->~mp3_decoder_t();
    new(dec) mp3_decoder_t(); // init c++ stuff
    dec->allocator = alloc;
    dec->jobs_sys = jobs;

    mp3dec_init(&dec->job_state.mp3d);
}

//---------------------------------------------------------------------------------------
// job

static void consume_rest_input(mp3_decoder_t::job_state_t* state) {
    // keep the rest part of input
    if (0 < state->input.buffer.size) {
        assert(state->input.buffer.size <= sizeof(state->aux_input_buf));

        memcpy(state->aux_input_buf, state->input.buffer.data, state->input.buffer.size);
        state->aux_input_size = state->input.buffer.size;
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

    assert(state->input.buffer.size);

    if (state->aux_input_size && !state->aux_input.data) {
        if (state->aux_input_size < MIN_DATA_CHUNK_SIZE) {
            auto input_moved_to_aux_size = MIN_DATA_CHUNK_SIZE - state->aux_input_size;
            input_moved_to_aux_size = input_moved_to_aux_size < state->input.buffer.size ? input_moved_to_aux_size : state->input.buffer.size;
            memcpy(&state->aux_input_buf[state->aux_input_size], state->input.buffer.data, input_moved_to_aux_size);

            data_buffer_t aux_input = {};
            aux_input.data = state->aux_input_buf;
            aux_input.size = state->aux_input_size + input_moved_to_aux_size;
            state->aux_input = aux_input;
        }
    }
    
    while(state->output_indices.can_write()) {

        if (is_empty(state->input.buffer)) {
            consume_rest_input(state);
            break;
        }

        auto input_ptr = state->aux_input.data ? &state->aux_input : &state->input.buffer;

        auto wp = state->output_indices.write_pos.load();
        auto& output = state->outputs[wp & (MAX_OUTPUT_BUFFERS - 1)];

        mp3dec_frame_info_t info;
        output.frame_offset = 0;
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
                state->input.buffer = advance(state->input.buffer, aux_processed_bytes - state->aux_input_size);

                // reset aux buffer
                state->aux_input_size = 0;
                state->aux_input = {};
            }
        }

        state->output_indices.write_pos.store(++wp);

        // consume input if less than 5 mp3 frames data left
        if (is_empty(state->input.buffer) ||
            (!state->input.last && (state->input.buffer.size < MIN_DATA_CHUNK_SIZE / 2))) {

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
        for (auto i = consumed_input_count; i < dec->input_count; ++i) {
            dec->inputs[i - consumed_input_count] = dec->inputs[i];
        }
        dec->input_count -= consumed_input_count;
        dec->consumed_input_count = 0;        
    }

    return consumed_input_count;
}

static void reset_inputs(mp3_decoder_t::job_state_t* state) {
    state->output_indices.reset();
    state->input = {};

    // reset aux buffer
    state->aux_input_size = 0;
    state->aux_input = {};

    state->not_enough_input_data = false;

    mp3dec_init(&state->mp3d);
}

static void kick_decoding_job(mp3_decoder_t* dec) {
    if (dec->job_state.running) return;

    if (dec->reset_state) {
        dec->reset_state = false;
        reset_inputs(&dec->job_state);
    }

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

    if (is_empty(dec->job_state.input.buffer)) {
        dec->job_state.input = dec->inputs[dec->consumed_input_count];
    }

    assert(dec->job_state.input.buffer.size);

    // launch decoder job
    dec->job_state.running = true;

    hlea_job_t job  = {};
    job.job_func = decode_mp3_jobfunc;
    job.udata = &dec->job_state;
    launch(dec->jobs_sys, job);
}

static bool queue_input(mp3_decoder_t* state, const data_buffer_t& buf, bool last_input) {
    // should not be the case ever, ?assert?
    if (state->input_count == MAX_INPUT_BUFFERS) return false;

    input_buffer_t input = {};
    input.buffer = buf;
    input.last = last_input;
    state->inputs[state->input_count++] = input;

    // launch job 
    kick_decoding_job(state);

    return true;
}

static bool contains(const output_buffer_t& out_buf, const void* ptr) {
    auto ptr_u8 = (uint8_t*)ptr;
    return (uint8_t*)out_buf.pcm <= ptr_u8 && ptr_u8 < ((uint8_t*)out_buf.pcm + sizeof(out_buf.pcm));
}

static void release_output(mp3_decoder_t* dec, data_buffer_t output_buf) {
    if (output_buf.size) {
        auto rp = dec->job_state.output_indices.read_pos.load();
        assert(contains(dec->job_state.outputs[(rp & MAX_OUTPUT_BUFFERS - 1)], output_buf.data));
        
        dec->job_state.output_indices.read_pos.store(++rp);

        // we now have one vacant output buffer to decode into
        // todo: here it is possible to get into condition where job is almost finished, but running flag is still true
        // launch decoding job
        kick_decoding_job(dec);
    }
}

static data_buffer_t get_frames_output_buffer(const output_buffer_t& output) {
    assert(output.frame_offset < output.frame_count);

    auto sample_byte_size = sizeof(float) * output.channels;

    data_buffer_t res = {};
    res.data = (uint8_t*)output.pcm + output.frame_offset * sample_byte_size;
    res.size = (output.frame_count - output.frame_offset) * sample_byte_size;
    return res;
}

static data_buffer_t next_output(mp3_decoder_t* dec, const data_buffer_t& current_buf) {
    

    // release previous buffer
    release_output(dec, current_buf);

    auto rp = dec->job_state.output_indices.read_pos.load();
    
    // return empty if next is still writing
    auto wp = dec->job_state.output_indices.write_pos.load();
    if (rp == wp) {
        return {};
    }

    uint8_t read_buf_index = rp & (MAX_OUTPUT_BUFFERS - 1);

    return get_frames_output_buffer(dec->job_state.outputs[read_buf_index]);
}

static bool is_running(const mp3_decoder_t* dec) {
    return dec->job_state.running;
}

static void flush(mp3_decoder_t* dec) {
    if (dec->job_state.running) {
        dec->job_state.stop_requested = true;
    }
    dec->reset_state = true;
    dec->consumed_input_count = 0;
    dec->input_count = 0;
}

//
// decoder_ti vtable
//

static size_t mp3dec_release_consumed_inputs(void* state) {
    auto dec = (mp3_decoder_t*)state;
    return release_consumed_inputs(dec);
}

static bool mp3dec_queue_input(void* state, const data_buffer_t& buf, bool last_input) {
    auto dec = (mp3_decoder_t*)state;
    return queue_input(dec, buf, last_input);
}

static data_buffer_t mp3dec_next_output(void* state, const data_buffer_t& current_buf) {
    auto dec = (mp3_decoder_t*)state;
    return next_output(dec, current_buf);
}

static bool mp3dec_is_running(void* state) {
    auto dec = (mp3_decoder_t*)state;
    return is_running(dec);
}

static void mp3dec_flush(void* state) {
    auto dec = (mp3_decoder_t*)state;
    flush(dec);
}

static void mp3dec_destroy(void* state) {
    auto dec = (mp3_decoder_t*)state;
    destroy(dec);
}

static const decoder_ti g_mp3_decoder_vt = []() {
    decoder_ti vt = {};
    vt.release_consumed_inputs = mp3dec_release_consumed_inputs;
    vt.queue_input = mp3dec_queue_input;
    vt.next_output = mp3dec_next_output;
    vt.is_running = mp3dec_is_running;
    vt.flush = mp3dec_flush;
    vt.destroy = mp3dec_destroy;

    return vt;
}();

decoder_t cast_to_decoder(mp3_decoder_t* dec) {
    decoder_t res = {};
    res.vt = &g_mp3_decoder_vt;
    res.state = dec;
    return res;
}

}
}
