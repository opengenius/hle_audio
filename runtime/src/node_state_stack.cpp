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
    if (!stack._top_entry) return;

    stack.top_entry_size--;

    if (stack.top_entry_size == 0) {
        auto prev_top = stack._top_entry->prev;
        deallocate(&stack.alloc, stack._top_entry);
        stack._top_entry = prev_top;
        stack.top_entry_size = prev_top ? state_stack_entry_t::ENTRY_NODE_COUNT : 0;
    }
}

void push_state(node_state_stack_t& stack, 
            const hle_audio::rt::node_desc_t& node_desc, const memory_layout_t& layout) {
    if (stack.top_entry_size == 0 ||
            stack.top_entry_size == state_stack_entry_t::ENTRY_NODE_COUNT) {
        stack.top_entry_size = 0;
        auto new_stack_entry = allocate<state_stack_entry_t>(&stack.alloc);
        new_stack_entry->prev = stack._top_entry;
        stack._top_entry = new_stack_entry;
    }

    void* state = allocate(&stack.alloc, layout.size, layout.alignment);
    memset(state, 0, layout.size);

    stack._top_entry->node_desc[stack.top_entry_size] = node_desc;
    stack._top_entry->state_data[stack.top_entry_size] = state;

    ++stack.top_entry_size;    
}

}
