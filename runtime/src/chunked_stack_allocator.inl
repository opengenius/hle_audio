#pragma once

#include "alloc_utils.inl"
#include "internal/memory_utils.inl"

namespace hle_audio {

static void init(chunked_stack_allocator_t* inst, uint32_t chunk_size, const allocator_t& backing_alloc) {
    *inst = {};
    inst->backing_alloc = backing_alloc;
    inst->chunk_size = chunk_size;
}

static void deinit(chunked_stack_allocator_t* inst) {
    while(inst->top_chunk) {
        // not found in current chunk, so pop it out
        auto chunk = inst->top_chunk;
        inst->top_chunk = chunk->prev;
        deallocate(inst->backing_alloc, chunk);
    }
}

static uint8_t* ptr_for_offset(chunked_stack_allocator_t* inst, size_t offset) {
    return (uint8_t*)inst->top_chunk + offset;
}

static void* allocate(chunked_stack_allocator_t* inst,
        uint32_t size, uint32_t alignment) {

    // size is bigger than max possible(todo: this doesn't consider alignment)
    if (inst->chunk_size < size + sizeof(chunked_stack_allocator_t::chunk_t)) return nullptr;

    auto top_ptr = ptr_for_offset(inst, inst->top_offset);
    uint8_t* top_chunk_end = ptr_for_offset(inst, inst->chunk_size);
    if (!inst->top_chunk ||
            top_chunk_end < align_forward(top_ptr, alignment) + size) {
        auto new_chunk = (chunked_stack_allocator_t::chunk_t*)allocate(inst->backing_alloc, inst->chunk_size);
        new_chunk->prev = inst->top_chunk;
        inst->top_chunk = new_chunk;
        inst->top_offset = sizeof(chunked_stack_allocator_t::chunk_t);
    }

    auto res = align_forward(ptr_for_offset(inst, inst->top_offset), alignment);
    inst->top_offset = res - (uint8_t*)inst->top_chunk + size;

    return res;
}

template<typename T>
static inline T* allocate(chunked_stack_allocator_t* inst) {
    return (T*)allocate(inst, sizeof(T), alignof(T));
}

static void deallocate(chunked_stack_allocator_t* inst,
        void* ptr) {
    
    while(inst->top_chunk) {
        uint8_t* top_chunk_begin = ptr_for_offset(inst, 0);
        uint8_t* top_chunk_end = ptr_for_offset(inst, inst->chunk_size);

        auto top_ptr = ptr_for_offset(inst, inst->top_offset);

        // detect trying to deallocate in unused chunk range
        assert(!(top_ptr <= ptr && ptr < top_chunk_end));

        if (top_chunk_begin <= ptr && ptr < top_ptr) {
            inst->top_offset = (uint32_t)((uint8_t*)ptr - top_chunk_begin);
            break;
        }

        // not found in current chunk, so pop it out
        auto chunk = inst->top_chunk;
        inst->top_chunk = chunk->prev;
        deallocate(inst->backing_alloc, chunk);
    }
}

}
