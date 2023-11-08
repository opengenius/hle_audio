#pragma once

#include <cstdint>

// https://stackoverflow.com/a/50978188

template<typename T>
T xorshift(const T& n, int i) {
    return n ^ (n >> i);
}

// a hash function with another name as to not confuse with std::hash
uint32_t distribute(const uint32_t& n) {
    uint32_t p = 0x55555555ul; // pattern of alternating 0 and 1
    uint32_t c = 3423571495ul; // random uneven integer constant; 
    return c * xorshift(p * xorshift(n, 16), 16);
}

// if c++20 rotl is not available:
template <typename T, typename S>
typename std::enable_if<std::is_unsigned<T>::value, T>::type
constexpr rotl(const T n, const S i) {
    const T m = (std::numeric_limits<T>::digits - 1);
    const T c = i & m;
    return (n << c) | (n >> ((T(0) - c) & m)); // this is usually recognized by the compiler to mean rotation, also c++20 now gives us rotl directly
}

// call this function with the old seed and the new key to be hashed and combined into the new seed value, respectively the final hash
static uint32_t hash_combine(uint32_t seed, uint32_t v) {
    return rotl(seed, std::numeric_limits<uint32_t>::digits/3) ^ distribute(v);
}