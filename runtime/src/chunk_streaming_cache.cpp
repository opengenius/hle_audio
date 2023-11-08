#include "chunk_streaming_cache.h"

#include <mutex>

#include "alloc_utils.inl"
#include "hash_indices.inl"
#include "hash_utils.inl"
#include "index_list.inl"

static const size_t READ_CHUNK_SIZE = 64 * 1024; // 64KB
static const size_t MAX_SOURCES_COUNT = 512;
static const size_t MAX_POOL_CHUNKS = 32; // 2MB total

namespace hle_audio {
namespace rt {

struct index_with_generation_t {
    uint16_t index;
    uint16_t generation;
};

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

    uint32_t chunk_indices_storage[MAX_POOL_CHUNKS * 4]; // hashes + indices storage
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

    // expect 0.5 as max load factor, so double chunk count should be enough (MAX_POOL_CHUNKS * 2 hash table slots)
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
    auto ch_index = find_index(&cache.chunk_indices, req_key_hash, 
            [&request, req_src_offset, &cache](uint32_t index)->bool {
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
