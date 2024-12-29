#include "tracking_allocator.h"
#include <mutex>
#include "alloc_utils.inl"

namespace hle_audio {

static std::mutex g_counter_mutex;

static void* tracking_allocator_allocate(void* udata, size_t size, size_t alignment) {
    auto tracking_alloc = (tracking_allocator_t*)udata;
    
    {
        std::unique_lock<std::mutex> lk(g_counter_mutex);
        ++tracking_alloc->counter;
    }
    return allocate(tracking_alloc->backing_alloc, size, alignment);
}

static void* tracking_allocator_reallocate(void* udata, void* p, size_t size) {
    auto tracking_alloc = (tracking_allocator_t*)udata;

    return reallocate(tracking_alloc->backing_alloc, p, size);
}

static void tracking_allocator_deallocate(void* udata, void* p) {
    auto tracking_alloc = (tracking_allocator_t*)udata;

    {
        std::unique_lock<std::mutex> lk(g_counter_mutex);
        --tracking_alloc->counter;
    }
    return deallocate(tracking_alloc->backing_alloc, p);
}

static const hlea_allocator_ti g_tracking_allocator_vt  = {
    tracking_allocator_allocate,
    tracking_allocator_reallocate,
    tracking_allocator_deallocate
};

allocator_t to_allocator(tracking_allocator_t* tracking_alloc) {
    return {&g_tracking_allocator_vt, tracking_alloc};
}

}
