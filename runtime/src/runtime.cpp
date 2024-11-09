#include "hlea/runtime.h"

#include <cstring>
#include <cassert>
#include <cstdio>
#include <algorithm>

#include "miniaudio_public.h"

#include "default_allocator.h"
#include "decoder.h"
#include "async_file_reader.h"
#include "internal_types.h"
#include "chunk_streaming_cache.h"
#include "decoder_mp3.h"
#include "decoder_pcm.h"

#include "alloc_utils.inl"
#include "jobs_utils.inl"
#include "file_utils.inl"
#include "allocator_bridge.inl"

/**
 * streaming TODOs:
 *  - implement decoder_ti for ogg, etc
 *  - loop range support with decoder_ti is tricky (async decoding to start position doesn't help to maintain gapless playback)
 *  - make MAX_POOL_CHUNKS configurable (chunk_streaming_cache.cpp)
 *  - add configurable dynamic chunks pool to overflow default pool budget
 */

using hle_audio::rt::data_buffer_t;
using hle_audio::rt::buffer_t;
using hle_audio::rt::file_data_t;
using hle_audio::rt::array_view_t;
using hle_audio::rt::root_header_t;
using hle_audio::rt::event_t;
using hle_audio::rt::action_type_e;
using hle_audio::rt::async_file_reader_t;
using hle_audio::rt::async_file_handle_t;
using hle_audio::rt::editor_runtime_t;

// runtime_groups.cpp
void fire_event(hlea_context_t* ctx, hlea_action_type_e event_type, const event_desc_t* desc);
void group_release_all_in_bank(hlea_context_t* ctx, const hlea_event_bank_t* bank);
void process_pending_sounds(hlea_context_t* ctx);

/////////////////////////////////////////////////////////////////////////////////////////

static ma_result task_executor_job_process(ma_job* pJob) {
    hlea_job_t job = {};
    job.job_func = (decltype(job.job_func))pJob->data.custom.data0;
    job.udata = (decltype(job.udata))pJob->data.custom.data1;

    job.job_func(job.udata);

    return MA_SUCCESS;
}

static void task_executor_launch(void* udata, hlea_job_t job) {
    auto executor = (ma_device_job_thread*)udata;

    auto ma_job = ma_job_init(MA_JOB_TYPE_CUSTOM);
    ma_job.data.custom.proc = task_executor_job_process;
    ma_job.data.custom.data0 = (ma_uintptr)job.job_func;
    ma_job.data.custom.data1 = (ma_uintptr)job.udata;
    auto result = ma_device_job_thread_post(executor, &ma_job);
}

static const hlea_jobs_ti s_task_executor_jobs_vt {
    task_executor_launch
};

hlea_context_t* hlea_create(hlea_context_create_info_t* info) {

    allocator_t alloc = hle_audio::make_default_allocator();
    if (info->allocator_vt) {
        alloc = allocator_t{info->allocator_vt, info->allocator_udata};
    }

    auto ctx = allocate_unique<hlea_context_t>(alloc);
    memset(ctx.get(), 0, sizeof(hlea_context_t));
    ctx->allocator = alloc;

    auto allocation_callbacks = make_allocation_callbacks(&ctx->allocator);

    // setup runtime common vfs
    ma_default_vfs_init(&ctx->vfs_default, &allocation_callbacks);
    ctx->pVFS = &ctx->vfs_default;

    auto config = ma_engine_config_init();
    if (info->file_api_vt) {
        init(ctx->vfs_impl, info->file_api_vt, info->file_sys);
        ctx->pVFS = &ctx->vfs_impl;
    }
    config.allocationCallbacks = allocation_callbacks;

    ma_result result = ma_engine_init(&config, &ctx->engine);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize audio engine.");

        return nullptr;
    }

    ctx->output_bus_group_count = (info->output_bus_count <= MAX_OUPUT_BUSES) ? info->output_bus_count : MAX_OUPUT_BUSES;
    for (size_t i = 0; i < ctx->output_bus_group_count; ++i) {
        // todo: check results, deinit, return nullptr
        result = ma_sound_group_init(&ctx->engine, 0, nullptr, &ctx->output_bus_groups[i]);
    }
    
    if (info->jobs_vt) {
        jobs_t jobs_impl = {};
        jobs_impl.vt = info->jobs_vt;
        jobs_impl.udata = info->jobs_udata;
        
        ctx->jobs = jobs_impl;
    } else {
        // init single thread pool as default job processor
        auto jobThreadConfig = ma_device_job_thread_config_init();
        ma_device_job_thread_init(&jobThreadConfig, &config.allocationCallbacks, &ctx->task_executor);

        jobs_t jobs_impl = {};
        jobs_impl.vt = &s_task_executor_jobs_vt;
        jobs_impl.udata = &ctx->task_executor;
        
        ctx->jobs = jobs_impl;
    }

    hle_audio::rt::async_file_reader_create_info_t cinfo = {};
    cinfo.allocator = ctx->allocator;
    cinfo.vfs = ctx->pVFS;
    ctx->async_io = hle_audio::rt::create_async_file_reader(cinfo);

    hle_audio::rt::chunk_streaming_cache_init_info_t cache_iinfo = {};
    cache_iinfo.allocator = ctx->allocator;
    cache_iinfo.async_io = ctx->async_io;
    ctx->streaming_cache = hle_audio::rt::create_cache(cache_iinfo);

    return ctx.release();
}

void hlea_destroy(hlea_context_t* ctx) {
    destroy(ctx->streaming_cache);
    destroy(ctx->async_io);

    if (ctx->jobs.vt == &s_task_executor_jobs_vt) {
        auto alloc_cb = make_allocation_callbacks(&ctx->allocator);
        ma_device_job_thread_uninit(&ctx->task_executor, &alloc_cb);
    }

    for (size_t i = 0; i < ctx->output_bus_group_count; ++i) {
        ma_sound_group_uninit(&ctx->output_bus_groups[i]);
    }
    ma_engine_uninit(&ctx->engine);

    deallocate(ctx->allocator, ctx);
}

void hlea_suspend(hlea_context_t* ctx) {
    ma_engine_stop(&ctx->engine);
}

void hlea_wakeup(hlea_context_t* ctx) {
    ma_engine_start(&ctx->engine);
}

/**
 * init with allocated buffer
 */
static hlea_event_bank_t* load_events_bank_buffer(hlea_context_t* ctx, void* pData) {
    auto data_header = (root_header_t*)pData;
    if (data_header->version != hle_audio::rt::STORE_BLOB_VERSION) {
        deallocate(ctx->allocator, pData);
        return nullptr;
    }

    buffer_t buf = {};
    buf.ptr = pData;

    auto store = data_header->store.get_ptr(buf);

    // todo: small allocation
    auto bank = allocate<hlea_event_bank_t>(ctx->allocator);

    *bank = {};
    bank->data_buffer_ptr = buf;
    bank->static_data = store;

    return bank;
}

hlea_event_bank_t* hlea_load_events_bank(hlea_context_t* ctx, const char* bank_filename, const char* stream_bank_filename) {
    data_buffer_t buffer = {};
    ma_result result = read_file(ctx->pVFS, bank_filename, ctx->allocator, &buffer);

    hlea_event_bank_t* res = load_events_bank_buffer(ctx, buffer.data);

    result = ma_vfs_open(ctx->pVFS, stream_bank_filename, MA_OPEN_MODE_READ, &res->streaming_file);
    if (result == MA_SUCCESS) {
        res->streaming_afile = start_async_reading(ctx->async_io, res->streaming_file);
        res->streaming_cache_src = register_source(ctx->streaming_cache, res->streaming_afile);
    } else {
        // couldn't open file, do nothing here
    } 

    return res;
}

hlea_event_bank_t* hlea_load_events_bank_from_buffer(hlea_context_t* ctx, const uint8_t* buf, size_t buf_size) {
    auto internal_buf = allocate(ctx->allocator, buf_size);
    memcpy(internal_buf, buf, buf_size);

    return load_events_bank_buffer(ctx, internal_buf);
}

void hlea_unload_events_bank(hlea_context_t* ctx, hlea_event_bank_t* bank) {
    // stop all sounds from bank
    group_release_all_in_bank(ctx, bank);

    if (bank->streaming_afile) {
        // safe as all sounds're stopped feeding from the stream
        deregister_source(ctx->streaming_cache, bank->streaming_cache_src);
        bank->streaming_cache_src = {};

        // wait for all reads to finish and stop
        stop_async_reading(ctx->async_io, bank->streaming_afile);
        bank->streaming_afile = {};

        // no more pending reads, close the file
        ma_vfs_close(ctx->pVFS, bank->streaming_file);
        bank->streaming_file = {};
    }

    // todo: push decoder could be using data_buffer_ptr (not yet the case), so need to keep buffer until 
    deallocate(ctx->allocator, bank->data_buffer_ptr.ptr);
    deallocate(ctx->allocator, bank);
}

void hlea_process_frame(hlea_context_t* ctx) {
    update_pending_reads(ctx->streaming_cache);
    hlea_process_active_groups(ctx);
    process_pending_sounds(ctx);
}

void hlea_fire_event(hlea_context_t* ctx, hlea_event_bank_t* bank, const char* eventName, uint32_t obj_id) {
    // find event with binary search
    // todo: replace with hash index
    auto buf_ptr = bank->data_buffer_ptr;
    auto event_offset_begin = bank->static_data->events.elements.get_ptr(buf_ptr);
    auto event_offset_end = event_offset_begin + bank->static_data->events.count;
    auto event = std::lower_bound(event_offset_begin, event_offset_end,
            eventName, [buf_ptr](const event_t& event, const char* str)
            {
                auto event_name = event.name.get_ptr(buf_ptr);
                return strcmp(event_name, str) < 0;
            });
    if (event == event_offset_end || strcmp(eventName, event->name.get_ptr(buf_ptr)) != 0) return;

    auto actions_size = event->actions.count;
    auto actions = event->actions.elements.get_ptr(buf_ptr);
    for (uint32_t action_index = 0u; action_index < actions_size; ++action_index) {
        auto action = &actions[action_index];
        if (action->type == action_type_e::none) continue;

        event_desc_t desc = {};
        desc.bank = bank;
        desc.target_index = action->target_index;
        desc.obj_id = obj_id;
        desc.fade_time = action->fade_time;

        auto type = (hlea_action_type_e)((int)(action->type) - 1);
        fire_event(ctx, type, &desc);
    }
}

void hlea_fire_event(hlea_context_t* ctx, const hlea_fire_event_info_t* event_info) {
    assert(event_info);

    for (uint32_t action_index = 0u; action_index< event_info->action_count; ++action_index) {
        auto& action = event_info->actions[action_index];

        event_desc_t desc = {};
        desc.bank = event_info->bank;
        desc.target_index = action.target_index;
        desc.obj_id = event_info->obj_id;
        desc.fade_time = action.fade_time;

        fire_event(ctx, action.type, &desc);
    }
}

void hlea_set_main_volume(hlea_context_t* ctx, float volume) {
    ma_engine_set_volume(&ctx->engine, volume);   
}

void hlea_set_bus_volume(hlea_context_t* ctx, uint8_t bus_index, float volume) {
    ma_sound_group_set_volume(&ctx->output_bus_groups[bus_index], volume);
}

/**************************************************************************************************
 * editor api
 */

size_t hlea_get_active_groups_count(hlea_context_t* ctx) {
    return ctx->active_groups_size;
}

size_t hlea_get_active_groups_infos(hlea_context_t* ctx, hlea_group_info_t* out_infos, size_t out_infos_size) {
    const size_t out_groups_count = (out_infos_size < ctx->active_groups_size) ? out_infos_size : ctx->active_groups_size;

    for (size_t active_index = 0u; active_index < out_groups_count; ++active_index) {
        group_data_t& group = ctx->active_groups[active_index];

        auto& out_info = out_infos[active_index];
        out_info.group_index = group.group_index;
        out_info.paused = (group.state == playing_state_e::PAUSED);
    }

    return out_groups_count;
}
