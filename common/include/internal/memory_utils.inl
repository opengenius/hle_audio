#pragma once

#include <cstdint>

namespace hle_audio {

template<typename T>
static inline T align_forward(T p, uint32_t align) {
    assert((align & (align - 1)) == 0);

    uintptr_t pi = (uintptr_t(p) + (align - 1)) & ~(uintptr_t(align) - 1);
    return (T)pi;
}

template<typename T>
static inline bool is_aligned(const T* p) {
    return (std::uintptr_t(p) & (alignof(T) - 1)) == 0;
}

}
