#pragma once

namespace hle_audio {
namespace rt {

struct hash_indices_t {
    uint32_t* hashes;
    uint32_t* indices;
    uint32_t  size;
    uint32_t  count;
};

struct hash32_find_iter_t {
    uint32_t index;
    uint32_t hash;
    uint32_t counter;
};

// todo: duplication from async_file_reader.cpp
constexpr bool is_power_of_2(size_t v) {
    return v && ((v & (v - 1)) == 0);
}

static void init(hash_indices_t* desc, uint32_t* hashes, uint32_t* indices, uint32_t size) {
    assert(is_power_of_2(size) && "size is expected to be power of 2");
    
    desc->hashes = hashes;
    desc->indices = indices;
    desc->size = size;
    desc->count = 0u;

    memset(hashes, 0, size * sizeof(uint32_t));
    // todo: ? use FF as invalid hash or MAX_UINT index value ?
}

static hash32_find_iter_t find_index(const hash_indices_t* desc, uint32_t key_hash, uint32_t* found_index) {
    assert(desc->hashes);
    assert(key_hash);

    hash32_find_iter_t res = {};

    uint32_t index_wrap_mask = desc->size - 1;

    res.index = key_hash & index_wrap_mask;
    for (res.counter = 0; res.counter < desc->size; ++res.counter) {
        res.hash = desc->hashes[res.index];
        if (!res.hash || res.hash == key_hash) {

            *found_index = desc->indices[res.index];
            return res;
        }

        res.index = (res.index + 1) & index_wrap_mask;
    }

    res.index = ~0u;
    *found_index = ~0u;

    return res;
}

static hash32_find_iter_t find_next(const hash_indices_t* desc, const hash32_find_iter_t* prev_iter, uint32_t* found_index) {
    assert(desc->hashes);
    assert(prev_iter->hash);

    hash32_find_iter_t res = {};

    uint32_t index_wrap_mask = desc->size - 1;

    res.index = (prev_iter->index + 1) & index_wrap_mask;
    for (res.counter = prev_iter->counter + 1; res.counter < desc->size; ++res.counter) {
        res.hash = desc->hashes[res.index];
        if (!res.hash || res.hash == prev_iter->hash) {
            *found_index = desc->indices[res.index];
            return res;
        }

        res.index = (res.index + 1) & index_wrap_mask;
    }

    res.index = ~0u;
    *found_index = ~0u;

    return res;
}

static uint32_t find_empty_index(const hash_indices_t* desc, uint32_t key_hash) {
    assert(desc->hashes);

    uint32_t index_wrap_mask = desc->size - 1;

    uint32_t it_index = key_hash & index_wrap_mask;
    for (size_t i = 0; i < desc->size; ++i) {
        if (!desc->hashes[it_index]) {
            return it_index;
        }

        it_index = (it_index + 1) & index_wrap_mask;
    }

    return ~0u;
}

static void insert(hash_indices_t* self, uint32_t key_hash, uint32_t value) {
    auto ht_index = find_empty_index(self, key_hash);

    self->hashes[ht_index] = key_hash;
    self->indices[ht_index] = value;

    ++self->count;
}

static void erase(hash_indices_t* self, uint32_t ht_index) {
    assert(self->count);

    uint32_t index_wrap_mask = self->size - 1;

    uint32_t index = ht_index;
    for (uint32_t i = (index + 1) & index_wrap_mask; i != index; i = (i + 1) & index_wrap_mask) {
        if (!self->hashes[i]) break;

        auto i_index = self->hashes[i] & index_wrap_mask;

        // move if index is better position for slot i
        //  (if i_index (desired position) is bigger that i (current slot) and 
        //  less or equal to index (slot to empty))
        if ( (i > index && (i_index <= index || i_index > i)) ||
             (i < index && (i_index <= index && i_index > i))) { 
            // swap
            self->hashes[index] = self->hashes[i];
            self->indices[index] = self->indices[i];
            index = i;
        }
    }

    // reset slot
    self->hashes[index] = 0u;

    --self->count;
}

static void erase_with_index(hash_indices_t* self, uint32_t key_hash, uint32_t index_value) {
    uint32_t found_index = 0u;
    auto iter = find_index(self, key_hash, &found_index);
    for (; iter.hash == key_hash; 
            iter = find_next(self, &iter, &found_index)) {
        if (index_value == found_index) {
            // index found, erase
            erase(self, iter.index);
            break;
        }
    }
}

template <typename TF>
static uint32_t find_index(const hash_indices_t* indices, uint32_t key_hash, TF test_index_cb) {
    uint32_t found_index = 0u;
    auto iter = find_index(indices, key_hash, &found_index);
    for (; iter.hash == key_hash; 
            iter = find_next(indices, &iter, &found_index)) {
        // found_index should be valid for matched key hash
        if (test_index_cb(found_index)) {
            return found_index;
        }
    }

    return ~0u;
}

}
}
