#pragma once

#include "internal_alloc_types.h"

namespace hle_audio {
    struct NodeDesc;
}

struct state_stack_entry_t {
    state_stack_entry_t* prev;

    const hle_audio::NodeDesc *node_desc;
    void* state_data;
};

struct node_state_stack_t {
    chunked_stack_allocator_t alloc;
    state_stack_entry_t* top_entry;
};

void init(node_state_stack_t& stack, uint32_t chunk_size, const allocator_t& backing_alloc);
void deinit(node_state_stack_t& stack);

void pop_up_state(node_state_stack_t& stack);
void push_state(node_state_stack_t& stack, 
            const hle_audio::NodeDesc* node_desc, const memory_layout_t& layout);
