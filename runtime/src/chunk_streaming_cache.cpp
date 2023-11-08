#include "chunk_streaming_cache.h"

#include <mutex>

#include "alloc_utils.inl"

static const size_t READ_CHUNK_SIZE = 64 * 1024; // 64KB
static const size_t MAX_SOURCES_COUNT = 512;
static const size_t MAX_POOL_CHUNKS = 32; // 2MB total

namespace hle_audio {
namespace rt {

struct index_with_generation_t {
    uint16_t index;
    uint16_t generation;
};

struct index_list_entry_t {
    uint16_t prev;
    uint16_t next;
};
struct index_list_t {
    index_list_entry_t* entries;
    uint16_t head_index;
};

static void push_back(index_list_t* list, uint16_t index);

static void init(index_list_t* list, index_list_entry_t* entries, uint16_t head_index) {
    list->entries = entries;
    list->head_index = head_index;

    index_list_entry_t head = {};
    head.prev = head.next = head_index;
    entries[head_index] = head;

    for (uint16_t i = 0; i < head_index; ++i) {
        push_back(list, i);
    }
}

static void push_back(index_list_t* list, uint16_t index) {
    auto head = list->entries[list->head_index];

    assert(list->entries[head.prev].next == list->head_index);
    list->entries[head.prev].next = index;

    index_list_entry_t new_entry = {};
    new_entry.prev = head.prev;
    new_entry.next = list->head_index;
    list->entries[index] = new_entry;

    list->entries[list->head_index].prev = index;
}

static void erase(index_list_t* list, uint16_t index) {
    auto cur = list->entries[index];
    assert(list->entries[cur.prev].next == index);
    list->entries[cur.prev].next = cur.next;
    assert(list->entries[cur.next].prev == index);
    list->entries[cur.next].prev = cur.prev;
}

static uint16_t pop_front(index_list_t* list) {
    auto head = list->entries[list->head_index];
    // empty
    if (head.next == list->head_index) return ~0u;

    erase(list, head.next);

    return head.next;
}

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

/////////////////////////////////////////////////////////////////////////////////////////
// hash utils (https://stackoverflow.com/a/50978188)
/////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////

// chunks states: unused, filling(writing into buffer from file), reading (by decoder with refcounting)
struct chunk_streaming_cache_t {
    allocator_t allocator;
    async_file_reader_t* async_io;

    std::mutex sync_mutex;

    struct source_t {
        async_file_handle_t file;
        uint16_t generation;
    };
    source_t sources[MAX_SOURCES_COUNT];

    struct chunk_t {
        streaming_source_handle src;
        uint32_t src_offset;

        uint32_t use_count;
        async_read_token_t read_token;
    };

    chunk_t chunks[MAX_POOL_CHUNKS];
    uint8_t* chunks_buffer;

    index_list_entry_t free_chunk_entries[MAX_POOL_CHUNKS + 1];
    index_list_t free_chunks;

    uint32_t chunk_indices_storage[MAX_POOL_CHUNKS * 4];
    hash_indices_t chunk_indices;
};

static streaming_source_handle pack_streaming_source_handle(const index_with_generation_t& index) {
    return streaming_source_handle(
        (index.index + 1) << 16 |
        index.generation);
}

static index_with_generation_t unpack(streaming_source_handle h) {
    assert(h);

    index_with_generation_t index = {};
    index.index = uint16_t(uint32_t(h) >> 16) - 1;
    index.generation = uint16_t(h);

    return index;
}

static uint32_t hash_src_pos(streaming_source_handle src, uint32_t src_offset) {
    uint32_t res = hash_combine(distribute(src_offset), (uint32_t)src);
    res += (res == 0) ? 1u : 0u; // zero hash is used as free slot marker
    return res;
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

chunk_streaming_cache_t* create_cache(const chunk_streaming_cache_init_info_t& info) {
    auto cache = allocate<chunk_streaming_cache_t>(info.allocator);
    cache = new(cache) chunk_streaming_cache_t(); // init c++ members

    cache->allocator = info.allocator;
    cache->async_io = info.async_io;

    memset(cache->sources, 0, sizeof(cache->sources));
    memset(cache->chunks, 0, sizeof(cache->chunks));

    // todo: make single allocation
    cache->chunks_buffer = (uint8_t*)allocate(info.allocator, MAX_POOL_CHUNKS * READ_CHUNK_SIZE);

    init(&cache->free_chunks, cache->free_chunk_entries, MAX_POOL_CHUNKS);

    init(&cache->chunk_indices, 
        cache->chunk_indices_storage, &cache->chunk_indices_storage[MAX_POOL_CHUNKS * 2], 
        MAX_POOL_CHUNKS * 2);

    return cache;
}

void destroy(chunk_streaming_cache_t* cache) {
    // todo: make sure chunks_buffer is not used for reading
    deallocate(cache->allocator, cache->chunks_buffer);

    cache->~chunk_streaming_cache_t();
    deallocate(cache->allocator, cache);
}

streaming_source_handle register_source(chunk_streaming_cache_t* cache, async_file_handle_t file) {
    std::unique_lock<std::mutex> lk(cache->sync_mutex);

    for (auto& src : cache->sources) {
        if (src.file == invalid_async_file_handle) {
            src.file = file;

            index_with_generation_t index_gen = {};
            index_gen.index = &src - cache->sources; 
            index_gen.generation = src.generation;

            return pack_streaming_source_handle(index_gen);
        }
    }

    return {};
}

// todo: do something with files in flight?
void deregister_source(chunk_streaming_cache_t* cache, streaming_source_handle src) {
    std::unique_lock<std::mutex> lk(cache->sync_mutex);

    auto index = unpack(src);

    auto& src_data = cache->sources[index.index];
    assert(src_data.generation == index.generation);

    src_data.file = invalid_async_file_handle;
    ++src_data.generation;
}

chunk_request_result_t acquire_chunk(chunk_streaming_cache_t& cache, const chunk_request_t& request) {
    std::unique_lock<std::mutex> lk(cache.sync_mutex);

    chunk_request_result_t res = {};
    res.index = ~0u;

    auto rest_size = request.buffer_block.offset + request.buffer_block.size - request.block_offset;
    auto buf_size = rest_size < READ_CHUNK_SIZE ? rest_size : READ_CHUNK_SIZE;

    auto req_src_offset = request.buffer_block.offset + request.block_offset;

    // try find chunk in cache
    auto req_key_hash = hash_src_pos(request.src, req_src_offset);
    auto ch_index = find_index(&cache.chunk_indices, req_key_hash, [&request, req_src_offset, &cache](uint32_t index)->bool {
        auto& ch = cache.chunks[index];
        return ch.src == request.src && ch.src_offset == req_src_offset;
    });
    if (ch_index != ~0u) {
        auto& ch_ref = cache.chunks[ch_index];
        // if chunk is in the free list, detach it
        if (ch_ref.use_count == 0) {
            erase(&cache.free_chunks, ch_index);
        }
        ++ch_ref.use_count;

        data_buffer_t buffer = {};
        buffer.data = &cache.chunks_buffer[ch_index * READ_CHUNK_SIZE];
        buffer.size = buf_size;

        res.index = ch_index;
        res.data = buffer;
        // todo: do not check read_token after READY is got? (read_token overflow case)
        res.status = check_request_running(cache.async_io, ch_ref.read_token) ? chunk_status_e::READING : chunk_status_e::READY;            

        return res;
    }

    // no chunk in cache found, get unused one
    auto free_index = pop_front(&cache.free_chunks);
    if (free_index == ~0u) return res;

    // erase chunk index as new chunk is being prepared
    auto& ch_ref = cache.chunks[free_index];
    assert(ch_ref.use_count == 0);
    if (ch_ref.src) {
        auto key_hash = hash_src_pos(ch_ref.src, ch_ref.src_offset);
        
        erase_with_index(&cache.chunk_indices, key_hash, free_index);
    }

    chunk_streaming_cache_t::chunk_t new_ch = {};
    new_ch.src = request.src;
    new_ch.src_offset = req_src_offset;
    new_ch.use_count = 1;

    auto src_index = unpack(request.src);

    const auto& src_data = cache.sources[src_index.index];
    assert(src_data.generation == src_index.generation && "Accessing the source after deregister!");

    data_buffer_t buffer = {};
    buffer.data = &cache.chunks_buffer[free_index * READ_CHUNK_SIZE];
    buffer.size = buf_size;
    
    // queue async chunk reading
    async_read_request_t read_req = {};
    read_req.file = src_data.file;
    read_req.offset = req_src_offset;
    read_req.out_buffer = buffer;
    new_ch.read_token = request_read(cache.async_io, read_req); // todo: check result

    cache.chunks[free_index] = new_ch;
    insert(&cache.chunk_indices, req_key_hash, free_index);

    res.index = free_index;
    res.data = buffer;
    return res;
}

void release_chunk(chunk_streaming_cache_t& cache, uint32_t chunk_index) {
    std::unique_lock<std::mutex> lk(cache.sync_mutex);

    auto& ch_ref = cache.chunks[chunk_index];
    assert(0 != ch_ref.use_count);
    --ch_ref.use_count;

    if (ch_ref.use_count == 0) {
        push_back(&cache.free_chunks, chunk_index);
    }
}

chunk_status_e chunk_status(const chunk_streaming_cache_t& cache, uint32_t chunk_index) {
    auto& ch = cache.chunks[chunk_index];
    return check_request_running(cache.async_io, ch.read_token) ? chunk_status_e::READING : chunk_status_e::READY;
}

}}
