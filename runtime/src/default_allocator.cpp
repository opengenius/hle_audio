#include "default_allocator.h"
#include <cassert>
#include <cstdlib>

namespace hle_audio {

static void* default_malloc_allocate(void* udata, size_t size, size_t alignment) {
    assert(alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
    return malloc(size);
    // todo: no aligned_alloc on windows
    // return std::aligned_alloc(alignment, size);
}

static void* default_malloc_reallocate(void* udata, void* p, size_t size) {
    return realloc(p, size);
}

static void default_malloc_deallocate(void* udata, void* p) {
    free(p);
}

static const hlea_allocator_ti g_malloc_allocator_vt  = {
    default_malloc_allocate,
    default_malloc_reallocate,
    default_malloc_deallocate
};

allocator_t make_default_allocator() {
    return {&g_malloc_allocator_vt, nullptr};
}

}
