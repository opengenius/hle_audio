#pragma once

static void* allocator_bridge_malloc(size_t sz, void* pUserData) {
    auto alloc = (allocator_t*)pUserData;
    return allocate(*alloc, sz);
}

static void* allocator_bridge_realloc(void* p, size_t sz, void* pUserData) {
    auto alloc = (allocator_t*)pUserData;
    return reallocate(*alloc, p, sz);
}

static void allocator_bridge_free(void* p, void* pUserData) {
    auto alloc = (allocator_t*)pUserData;
    deallocate(*alloc, p);
}

static ma_allocation_callbacks make_allocation_callbacks(allocator_t* alloc) {
    static const ma_allocation_callbacks allocator_bridge_ma_cb_prototype = {
        nullptr,
        allocator_bridge_malloc,
        allocator_bridge_realloc,
        allocator_bridge_free
    };

    ma_allocation_callbacks res = allocator_bridge_ma_cb_prototype;
    res.pUserData = alloc;
    return res;
}
