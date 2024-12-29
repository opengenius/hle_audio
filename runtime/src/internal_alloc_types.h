#pragma once

#include "hlea/alloc_types.h"
#include <cstdint>

struct allocator_t {
    const hlea_allocator_ti* vt;
    void* udata;
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

struct memory_layout_t {
    size_t size;
    size_t alignment;
};

struct tracking_allocator_t {
    allocator_t backing_alloc;
    int counter;
};
