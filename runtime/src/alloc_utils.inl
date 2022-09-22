#pragma once

#include <cstdint>
#include "alloc_types.h"

struct allocator_t {
    const hlea_allocator_ti* allocator_vt;
    void* allocator_udata;
};

struct chunked_stack_allocator_t {
    struct chunk_t {
        chunk_t* prev;
    };

    allocator_t backing_alloc;

    chunk_t* top_chunk;
    uint32_t top_offset;
    uint32_t chunk_size;
};

static void* allocate(const allocator_t& alloc, size_t size, size_t alignment = alignof(std::max_align_t)) {
    return alloc.allocator_vt->allocate(alloc.allocator_udata, size, alignment);
}

template<typename T>
static inline T* allocate(const allocator_t& alloc) {
    return (T*)allocate(alloc, sizeof(T), alignof(T));
}

static void deallocate(const allocator_t& alloc, void* p);

struct allocator_deleter_t {
    allocator_t alloc;
    void operator()(void* p) const {
        deallocate(alloc, p);
    }
};

template<typename T>
static inline std::unique_ptr<T, allocator_deleter_t> allocate_unique(const allocator_t& alloc) {
    return std::unique_ptr<T, allocator_deleter_t>(allocate<T>(alloc), {alloc});
}

static void deallocate(const allocator_t& alloc, void* p) {
    alloc.allocator_vt->deallocate(alloc.allocator_udata, p);
}

template<typename T>
static inline T align_forward(T p, uint32_t align)
{
    assert((align & (align - 1)) == 0);

    uintptr_t pi = (uintptr_t(p) + (align - 1)) & ~(uintptr_t(align) - 1);
    return (T)pi;
}

//
// chunked_stack_allocator_t
//
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
