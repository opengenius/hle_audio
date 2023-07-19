#include "data_types.h"
#include "rt_types.h"
#include <vector>
#include <unordered_map>
#include <string_view>
#include <cassert>
#include <cstring>
#include <algorithm>
#include "internal/memory_utils.inl"

namespace hle_audio {

/////////////////////////////////////////////////////////////////////////////////////////
// buffer write helpers
/////////////////////////////////////////////////////////////////////////////////////////

template<typename T>
static rt::array_view_t<T> write(std::vector<uint8_t>& buf, rt::offset_typed_t<T> offset, const T* values, size_t count) {
    // expect buf data is enough aligned(alignof(T))
    auto buf_ptr = reinterpret_cast<T*>(buf.data() + offset.pos);
    assert(is_aligned(buf_ptr));

    for (size_t i = 0; i < count; ++i) {
        buf_ptr[i] = values[i];
    }
    
    return {(uint32_t)count, offset};
}

template<typename T>
static rt::array_view_t<T> write(std::vector<uint8_t>& buf, const T* values, size_t count) {
    auto res = (rt::offset_t)buf.size();

    res = align_forward(res, alignof(T));

    auto new_size = res + sizeof(T) * count;
    buf.resize(new_size);

    return write(buf, {res}, values, count);
}

template<typename T>
static rt::array_view_t<T> write(std::vector<uint8_t>& buf, const std::vector<T>& values) {
    return write(buf, values.data(), values.size());
}

template<typename T>
static rt::offset_typed_t<T> write_single(std::vector<uint8_t>& buf, const T& v) {
    return write(buf, &v, 1).elements;
}

static rt::char_offset_t write(std::vector<uint8_t>& buf, std::string_view str) {
    // this expects c string and copies trailing null as well
    assert(str.data()[str.size()] == '\0');
    return write(buf, str.data(), str.size() + 1).elements;
}

/////////////////////////////////////////////////////////////////////////////////////////

namespace editor {

struct save_context_t {
    std::vector<rt::char_offset_t> sound_files;
    std::vector<rt::file_node_t> nodes_file;
    std::vector<rt::random_node_t> nodes_random;
    std::vector<rt::sequence_node_t> nodes_sequence;
    std::vector<rt::repeat_node_t> nodes_repeat;

    std::unordered_map<std::string_view, uint32_t> sound_files_indices;
};

static rt::named_group_t make_named_group(std::vector<uint8_t>& buf, 
        std::string_view name,
        float volume,
        float cross_fade_time,
        uint8_t output_bus_index,
        const rt::node_desc_t& root_node) {

    rt::named_group_t gr = {};
    gr.name = write(buf, name);
    gr.volume = volume;
    gr.cross_fade_time = cross_fade_time;
    gr.output_bus_index = output_bus_index;
    gr.node = root_node;

    return gr;
}

static rt::event_t make_event(std::vector<uint8_t>& buf, 
        std::string_view name,
        const std::vector<rt::action_t>& actions) {
    rt::event_t ev = {};
    ev.name = write(buf, name);
    ev.actions = write(buf, actions);

    return ev;
}

static rt::offset_typed_t<rt::store_t> write_store(std::vector<uint8_t>& buf,
        const save_context_t& ctx,
        const std::vector<rt::named_group_t>& groups,
        const std::vector<rt::event_t>& events) {

    // todo: use char_offset_t instead of indexing into sound_files

    rt::store_t store = {};
    store.sound_files = write(buf, ctx.sound_files);
    store.nodes_file = write(buf, ctx.nodes_file);
    store.nodes_random = write(buf, ctx.nodes_random);
    store.nodes_sequence = write(buf, ctx.nodes_sequence);
    store.nodes_repeat = write(buf, ctx.nodes_repeat);
    store.groups = write(buf, groups);
    store.events = write(buf, events);

    return write_single(buf, store);
}

static uint32_t cache_filename_index(std::vector<uint8_t>& buf, save_context_t* ctx, std::string_view filename) {
    auto indices_it = ctx->sound_files_indices.find(filename);
    if (indices_it != ctx->sound_files_indices.end()) {
        return indices_it->second;
    }

    uint32_t index = (uint32_t)ctx->sound_files.size();
    ctx->sound_files_indices[filename] = index;
    ctx->sound_files.push_back(write(buf, filename));

    return index;
}

static rt::node_desc_t save_node_rec(std::vector<uint8_t>& buf, save_context_t* ctx, 
        const data_state_t* state, const node_desc_t& desc) {

    auto index = get_index(state->node_ids, desc.id);
    uint16_t out_index = 0;
    switch (desc.type)
    {
    case rt::node_type_e::None: {
        break;
    }
    case rt::node_type_e::File: {
        auto& file_node = state->nodes_file[index];

        out_index = (uint16_t)ctx->nodes_file.size();

        rt::file_node_t node = {};
        node.file_index = cache_filename_index(buf, ctx, file_node.filename);
        node.loop = file_node.loop;
        node.stream = file_node.stream;
        ctx->nodes_file.push_back(node);

        break;
    }
    case rt::node_type_e::Random: {
        auto& rnd_node = state->nodes_random[index];

        std::vector<rt::node_desc_t> ch_nodes;
        for (auto& ch_desc : rnd_node.nodes) {
            ch_nodes.push_back(save_node_rec(buf, ctx, state, ch_desc));
        }

        out_index = (uint16_t)ctx->nodes_random.size();

        rt::random_node_t node = {};
        node.nodes = write(buf, ch_nodes);
        ctx->nodes_random.push_back(node);

        break;
    }
    case rt::node_type_e::Sequence: {
        auto& seq_node = state->nodes_sequence[index];

        std::vector<rt::node_desc_t> ch_nodes;
        for (auto& ch_desc : seq_node.nodes) {
            ch_nodes.push_back(save_node_rec(buf, ctx, state, ch_desc));
        }

        out_index = (uint16_t)ctx->nodes_sequence.size();

        rt::sequence_node_t node = {};
        node.nodes = write(buf, ch_nodes);
        ctx->nodes_sequence.push_back(node);

        break;
    }
    case rt::node_type_e::Repeat: {
        auto& rep_node = state->nodes_repeat[index];

        auto ndesc = save_node_rec(buf, ctx, state, rep_node.node);

        out_index = (uint16_t)ctx->nodes_repeat.size();

        rt::repeat_node_t rt_node = {};
        rt_node.repeat_count = rep_node.repeat_count;
        rt_node.node = ndesc;
        ctx->nodes_repeat.push_back(rt_node);

        break;
    }
    default:
        assert(false);
        break;
    }

    return {desc.type, out_index};
}

static void save_store_blob(const data_state_t* state, std::vector<uint8_t>& buf) {

    rt::root_header_t header = {};
    auto store_header_offset = write_single(buf, header);
    assert(store_header_offset.pos == 0);

    save_context_t ctx = {};

    std::vector<rt::named_group_t> groups;
    groups.reserve(state->groups.size());
    for (auto& group : state->groups) {
        auto desc = save_node_rec(buf, &ctx, state, group.node);

        auto fbo_group = make_named_group(buf, 
            group.name,
            group.volume,
            group.cross_fade_time,
            group.output_bus_index,
            desc
        );
        groups.push_back(fbo_group);
    }

    std::vector<rt::event_t> events;
    events.reserve(state->events.size());
    for (auto& ev : state->events) {
        events.push_back(make_event(buf, ev .name, ev.actions));
    }

    // sort events by name
    rt::buffer_t buf_view = {};
    buf_view.ptr = buf.data();
    std::sort(events.begin(), events.end(), 
            [&buf_view](const rt::event_t& e1, 
                    const rt::event_t& e2) {
        return strcmp(
                e1.name.get_ptr(buf_view), 
                e2.name.get_ptr(buf_view)
            ) < 0;
    });

    auto store_offset = write_store(buf,
            ctx, groups, events);

    // write root offset finally
    header.store = store_offset;
    write(buf, store_header_offset, &header, 1);
}

std::vector<uint8_t> save_store_blob_buffer(const data_state_t* state) {
    std::vector<uint8_t> res;
    save_store_blob(state, res);

    return res;
}

}
}
