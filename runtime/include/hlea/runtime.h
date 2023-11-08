#pragma once

#include <cstdint>
#include "alloc_types.h"
#include "file_types.h"
#include "jobs_types.h"

struct hlea_event_bank_t;
struct hlea_context_t;

/**
 *  init/deinit context
 */
struct hlea_context_create_info_t {
    const hlea_file_ti* file_api_vt;
    void* file_sys;

    const hlea_allocator_ti* allocator_vt;
    void* allocator_udata;

    const hlea_jobs_ti* jobs_vt;
    void* jobs_udata;

    uint8_t output_bus_count;
};

hlea_context_t* hlea_create(hlea_context_create_info_t* info);
void hlea_destroy(hlea_context_t* ctx);

void hlea_suspend(hlea_context_t* ctx);
void hlea_wakeup(hlea_context_t* ctx);

/**
 * banks
 */
hlea_event_bank_t* hlea_load_events_bank(hlea_context_t* ctx, const char* bank_filename, const char* stream_bank_filename);
hlea_event_bank_t* hlea_load_events_bank_from_buffer(hlea_context_t* ctx, const uint8_t* buf, size_t buf_size);
void hlea_unload_events_bank(hlea_context_t* ctx, hlea_event_bank_t* bank);

/**
 * events api
 */
void hlea_process_active_groups(hlea_context_t* ctx);
void hlea_process_frame(hlea_context_t* ctx);

void hlea_fire_event(hlea_context_t* ctx, hlea_event_bank_t* bank, const char* eventName, uint32_t obj_id);

enum class hlea_action_type_e {
    play_single,
    play,
    stop,
    break_loop,
    pause,
    resume,
    pause_bus,
    resume_bus,
    stop_bus,
    stop_all
};

struct hlea_action_info_t {
    hlea_action_type_e type;
    size_t target_index;
    float fade_time;
};

struct hlea_fire_event_info_t {
    hlea_event_bank_t* bank;
    uint32_t obj_id;
    hlea_action_info_t* actions;
    size_t action_count;
};
void hlea_fire_event(hlea_context_t* ctx, const hlea_fire_event_info_t* event_info);

// volumes
void hlea_set_main_volume(hlea_context_t* ctx, float volume);
void hlea_set_bus_volume(hlea_context_t* ctx, uint8_t bus_index, float volume);

/** 
 * editor api
 * todo: move out of public header to its own implemenation files
 */

size_t hlea_get_active_groups_count(hlea_context_t* ctx);
struct hlea_group_info_t {
    size_t group_index;
    bool paused;
};
size_t hlea_get_active_groups_infos(hlea_context_t* ctx, hlea_group_info_t* out_infos, size_t out_infos_size);
