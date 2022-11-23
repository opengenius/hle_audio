#include "node_state_stack.h"
#include "chunked_stack_allocator.inl"

namespace hle_audio {

void init(node_state_stack_t& stack, uint32_t chunk_size, const allocator_t& backing_alloc) {
    init(&stack.alloc, chunk_size, backing_alloc);
}

void deinit(node_state_stack_t& stack) {
    deinit(&stack.alloc);
}

void pop_up_state(node_state_stack_t& stack) {
    if (!stack.top_entry) return;

    auto prev_top = stack.top_entry->prev;
    deallocate(&stack.alloc, stack.top_entry);
    stack.top_entry = prev_top;
}

void push_state(node_state_stack_t& stack, 
            const hle_audio::rt::node_desc_t* node_desc, const memory_layout_t& layout) {
    auto new_stack_entry = allocate<state_stack_entry_t>(&stack.alloc);
    new_stack_entry->node_desc = node_desc;

    void* node_state = allocate(&stack.alloc, layout.size, layout.alignment);
    new_stack_entry->state_data = node_state;

    new_stack_entry->prev = stack.top_entry;
    stack.top_entry = new_stack_entry;
}

}
