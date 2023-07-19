#pragma once

#include "internal_alloc_types.h"
#include "rt_types.h"

namespace hle_audio {

struct state_stack_entry_t {
    static const size_t ENTRY_NODE_COUNT = 4;

    state_stack_entry_t* prev;

    hle_audio::rt::node_desc_t node_desc[ENTRY_NODE_COUNT];
    void* state_data[ENTRY_NODE_COUNT];
};

static_assert(sizeof(state_stack_entry_t) <= 56, "try to fit in cache line");

struct node_state_stack_t {
    chunked_stack_allocator_t alloc;
    state_stack_entry_t* _top_entry;
    uint16_t top_entry_size;
};

void init(node_state_stack_t& stack, uint32_t chunk_size, const allocator_t& backing_alloc);
void deinit(node_state_stack_t& stack);

void pop_up_state(node_state_stack_t& stack);
void push_state(node_state_stack_t& stack, 
            const hle_audio::rt::node_desc_t& node_desc, const memory_layout_t& layout);

static bool is_empty(node_state_stack_t& stack) {
    return stack._top_entry == nullptr;
}

static hle_audio::rt::node_desc_t top_node_desc(const node_state_stack_t& stack) {
    assert(stack._top_entry);
    return stack._top_entry->node_desc[stack.top_entry_size - 1];
}

static void* top_state(const node_state_stack_t& stack) {
    assert(stack._top_entry);
    return stack._top_entry->state_data[stack.top_entry_size - 1];
}

}