#pragma once

#include <cstdint>
#include "alloc_types.h"

struct hlea_event_bank_t;
struct hlea_context_t;

enum hlea_file_handle_t : intptr_t;

struct hlea_file_ti {
    hlea_file_handle_t (*open)(void* sys, const char* file_path);
    void (*close)(void* sys, hlea_file_handle_t file);

    size_t (*size)(void* sys, hlea_file_handle_t file);

    size_t (*read)(void* sys, hlea_file_handle_t file, void* dst, size_t dst_size);

    size_t (*tell)(void* sys, hlea_file_handle_t file);
    void (*seek)(void* sys, hlea_file_handle_t file, size_t pos);
};

/**
 *  init/deinit context
 */
struct hlea_context_create_info_t {
    const hlea_file_ti* file_api_vt;
    void* file_sys;

    const hlea_allocator_ti* allocator_vt;
    void* allocator_udata;

    uint8_t output_bus_count;
};
hlea_context_t* hlea_create(hlea_context_create_info_t* info);
void hlea_destroy(hlea_context_t* impl_data);

void hlea_suspend(hlea_context_t* ctx);
void hlea_wakeup(hlea_context_t* ctx);

/**
 * banks
 */
hlea_event_bank_t* hlea_load_events_bank(hlea_context_t* impl_data, const char* bank_filename);
hlea_event_bank_t* hlea_load_events_bank_from_buffer(hlea_context_t* impl_data, const uint8_t* buf, size_t buf_size);
void hlea_unload_events_bank(hlea_context_t* impl_data, hlea_event_bank_t* bank);

/**
 * events api
 */
void hlea_process_active_groups(hlea_context_t* impl_data);

void hlea_fire_event(hlea_context_t* impl_data, hlea_event_bank_t* bank, const char* eventName, uint32_t obj_id);

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
void hlea_fire_event(hlea_context_t* impl_data, const hlea_fire_event_info_t* event_info);

// volumes
void hlea_set_main_volume(hlea_context_t* impl_data, float volume);
void hlea_set_bus_volume(hlea_context_t* impl_data, uint8_t bus_index, float volume);

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

void hlea_set_sounds_path(hlea_context_t* ctx, const char* sounds_path);
void hlea_play_file(hlea_context_t* ctx, const char* file_path);
bool hlea_is_file_playing(hlea_context_t* ctx);
void hlea_stop_file(hlea_context_t* ctx);
