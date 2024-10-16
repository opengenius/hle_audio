#include "async_file_reader.h"
#include "alloc_utils.inl"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <chrono>

#include "miniaudio_public.h"

static const bool ENABLE_DEBUG_READ_DELAY = false;
static const auto DEBUG_READ_DELAY = std::chrono::milliseconds(2000);
static const auto REQUESTS_WAIT_TIME = std::chrono::milliseconds(1);

namespace hle_audio {
namespace rt {

struct async_file_data_t {
    ma_vfs_file file;
};

struct ring_indices_u32_t {
    using indices_type = uint32_t;
    std::atomic<indices_type> read_pos;
    std::atomic<indices_type> write_pos;
};

static const size_t MAX_OPENED_FILES = 512;
static const size_t MAX_READ_REQUESTS = 512;

struct async_file_reader_t {
    allocator_t allocator;
    ma_vfs* vfs;

    async_file_data_t opened_files[MAX_OPENED_FILES];
    uint32_t opened_file_count;

    async_file_handle_t opened_files_freed[MAX_OPENED_FILES];
    size_t opened_files_freed_count;

    std::atomic<uint32_t> read_pos_processed;

    async_read_request_t read_requests[MAX_READ_REQUESTS];
    ring_indices_u32_t read_request_indices;
    std::mutex request_write_mutex;
    std::condition_variable request_signal;
    std::thread reading_thread;
    std::atomic<bool> stopped;
};

static bool can_read(const ring_indices_u32_t& indices) {
    return indices.read_pos != indices.write_pos;
}

// thread-safe
static bool can_write(const ring_indices_u32_t& indices, uint32_t range) {
    return indices.write_pos.load() != ring_indices_u32_t::indices_type(indices.read_pos.load() + range);
}

constexpr bool is_power_of_2(size_t v) {
    return v && ((v & (v - 1)) == 0);
}

static uint16_t to_request_index(uint32_t pos) {
    static_assert(is_power_of_2(MAX_READ_REQUESTS), "here MAX_READ_REQUESTS is expected to be power of 2");
    static_assert(MAX_READ_REQUESTS < std::numeric_limits<uint16_t>::max(), "MAX_READ_REQUESTS should fit within uint16_t");

    return pos & (MAX_READ_REQUESTS - 1);
}

static void process_async_reader(async_file_reader_t* reader) {
    while(!reader->stopped) {
        if (!can_read(reader->read_request_indices)) {
            // nothing to read, wait
            std::unique_lock<std::mutex> lk(reader->request_write_mutex);
            reader->request_signal.wait(lk, [reader]() {
                if (reader->stopped) return true;

                return can_read(reader->read_request_indices);
            });
        } else {
            auto rp = reader->read_request_indices.read_pos.load();
            auto req_index = to_request_index(rp);
            
            async_read_request_t req = reader->read_requests[req_index];
            reader->read_request_indices.read_pos++;

            // todo: ? sync with start_async_reading ?
            auto file = reader->opened_files[req.file - 1].file;

            if (ENABLE_DEBUG_READ_DELAY) {
                std::this_thread::sleep_for(DEBUG_READ_DELAY);
            }
            
            ma_vfs_seek(reader->vfs, file, req.offset, ma_seek_origin_start);
            size_t read_bytes = {};
            ma_vfs_read(reader->vfs, file, req.out_buffer.data, req.out_buffer.size, &read_bytes);

            reader->read_pos_processed = reader->read_request_indices.read_pos.load();
        }
    }
}

async_file_reader_t* create_async_file_reader(const async_file_reader_create_info_t& info) {
    auto res = allocate<async_file_reader_t>(info.allocator);
    res = new(res) async_file_reader_t();
    res->allocator = info.allocator;
    res->vfs = info.vfs;

    res->reading_thread = std::thread(process_async_reader, res);

    return res;
}

void destroy(async_file_reader_t* reader) {
    reader->stopped = true;
    reader->request_signal.notify_one();
    reader->reading_thread.join();

    reader->~async_file_reader_t();
    deallocate(reader->allocator, reader);
}

async_file_handle_t start_async_reading(async_file_reader_t* reader, ma_vfs_file f) {
    static std::thread::id this_id = std::this_thread::get_id();
    assert(this_id == std::this_thread::get_id() && "opened_file_count is not atomic");

    uint32_t file_index = {};
    if (reader->opened_files_freed_count) {
        auto recycled_h = reader->opened_files_freed[--reader->opened_files_freed_count];
        file_index = recycled_h - 1;
    } else if (reader->opened_file_count < MAX_OPENED_FILES) {
        file_index = reader->opened_file_count++;
    } else {
        return invalid_async_file_handle;
    }

    async_file_data_t fdata = {};
    fdata.file = f;
    reader->opened_files[file_index] = fdata;

    return async_file_handle_t(file_index + 1);
}

void stop_async_reading(async_file_reader_t* reader, async_file_handle_t afile) {
    // respect queued read requests, wait for all pending reads (too strict now - wait for reads even from other files)
    // todo: consider non-blocking solution, waiting read per file
    auto wp = async_read_token_t(reader->read_request_indices.write_pos.load());
    while (check_request_running(reader, wp)) {
        std::this_thread::sleep_for(REQUESTS_WAIT_TIME);
    }
    
    reader->opened_files_freed[reader->opened_files_freed_count++] = afile;
}

async_read_token_t request_read(async_file_reader_t* reader, const async_read_request_t& request) {
    
    async_read_token_t res = {};

    // extra loop to sleep outside locking request_write_mutex
    while (true) {
        while (!can_write(reader->read_request_indices, MAX_READ_REQUESTS)) {
            // read_requests is full, wait
            std::this_thread::sleep_for(REQUESTS_WAIT_TIME);
            // consider non-blocking solution: return invalid handle or fail code
        }

        // lock for potential requests from multiple threads
        std::unique_lock<std::mutex> lk(reader->request_write_mutex);

        if (!can_write(reader->read_request_indices, MAX_READ_REQUESTS)) continue;

        auto wp = reader->read_request_indices.write_pos.load();
        reader->read_requests[to_request_index(wp)] = request;
        ++wp;
        reader->read_request_indices.write_pos.store(wp);

        res = async_read_token_t(wp);

        break;
    }

    reader->request_signal.notify_one();

    return res;
}

// thread-safe
bool check_request_running(const async_file_reader_t* reader, async_read_token_t token) {
    auto last_req = reader->read_request_indices.write_pos.load();
    uint32_t tok_dist = last_req - uint32_t(token);
    uint32_t queued_dist = last_req - reader->read_pos_processed;

    return tok_dist < queued_dist;
}

}
}
