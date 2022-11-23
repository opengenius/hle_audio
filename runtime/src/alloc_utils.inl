#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <cassert>
#include "internal_alloc_types.h"

static void* allocate(const allocator_t& alloc, size_t size, size_t alignment = alignof(std::max_align_t)) {
    return alloc.vt->allocate(alloc.udata, size, alignment);
}

static void* reallocate(const allocator_t& alloc, void* p, size_t size) {
    return alloc.vt->reallocate(alloc.udata, p, size);
}

static void deallocate(const allocator_t& alloc, void* p) {
    alloc.vt->deallocate(alloc.udata, p);
}

template<typename T>
static inline T* allocate(const allocator_t& alloc) {
    return (T*)allocate(alloc, sizeof(T), alignof(T));
}

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

