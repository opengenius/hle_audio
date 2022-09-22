#pragma once

struct hlea_allocator_ti {
    void* (*allocate)(void* udata, size_t size, size_t alignment);
    void* (*reallocate)(void* udata, void* p, size_t size);
    void (*deallocate)(void* udata, void* memory);
};
