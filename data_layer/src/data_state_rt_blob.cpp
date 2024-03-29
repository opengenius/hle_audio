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
    if (!count) return {};

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

static rt::char_offset_t write(std::vector<uint8_t>& buf, std::u8string_view str) {
    // this expects c string and copies trailing null as well
    assert(str.data()[str.size()] == '\0');
    return write(buf, (const char*)str.data(), str.size() + 1).elements;
}

/////////////////////////////////////////////////////////////////////////////////////////

namespace editor {

struct save_context_t {
    std::vector<rt::file_node_t> nodes_file;
    std::vector<rt::random_node_t> nodes_random;
    std::vector<rt::sequence_node_t> nodes_sequence;
    std::vector<rt::repeat_node_t> nodes_repeat;
    std::vector<rt::file_data_t> file_data;

    struct file_data_t {
        bool stream;
        std::u8string_view filename;
    };
    std::vector<file_data_t> sound_file_data;
    std::unordered_map<std::u8string_view, uint32_t> sound_files_indices;
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
    store.nodes_file = write(buf, ctx.nodes_file);
    store.nodes_random = write(buf, ctx.nodes_random);
    store.nodes_sequence = write(buf, ctx.nodes_sequence);
    store.nodes_repeat = write(buf, ctx.nodes_repeat);
    store.groups = write(buf, groups);
    store.events = write(buf, events);
    store.file_data = write(buf, ctx.file_data);

    return write_single(buf, store);
}

static rt::file_node_t cache_file(std::vector<uint8_t>& buf, save_context_t* ctx, const file_node_t& file_node) {
    std::u8string_view filename = file_node.filename;

    uint32_t index = 0;

    auto indices_it = ctx->sound_files_indices.find(filename);
    if (indices_it != ctx->sound_files_indices.end()) {
        ctx->sound_file_data[indices_it->second].stream &= file_node.stream;
        index = indices_it->second;
    } else {
        index = (uint32_t)ctx->sound_file_data.size();
        ctx->sound_files_indices[filename] = index;
        
        save_context_t::file_data_t fdata = {};
        fdata.stream = file_node.stream;
        fdata.filename = filename;
        ctx->sound_file_data.push_back(fdata);
    }

    rt::file_node_t res = {};
    res.file_index = index;
    res.loop = file_node.loop;
    return res;
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

        ctx->nodes_file.push_back(cache_file(buf, ctx, file_node));

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

std::vector<uint8_t> save_store_blob_buffer(const data_state_t* state, audio_file_data_provider_ti* fdata_provider, const char* streaming_filename) {
    std::vector<uint8_t> buf;

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

    if (fdata_provider) {

        FILE* streaming_file = nullptr;
        if (streaming_filename) {
            streaming_file = fopen(streaming_filename, "wb");
            // expect valid?
        }


        uint32_t it_index = 0;
        for (auto& sound_file_data : ctx.sound_file_data) {
            auto fdata = fdata_provider->get_file_data((const char*)sound_file_data.filename.data(), it_index);
            auto stream = sound_file_data.stream;

            // write only data chunk
            auto content_data = fdata.content.data() + fdata.data_chunk_range.offset;
            auto content_data_size = fdata.data_chunk_range.size;

            rt::file_data_t rt_fd = {};
            rt_fd.meta = fdata.meta;
            rt_fd.meta.stream = stream ? 1 : 0;
            if (!stream) { 
                rt_fd.data_buffer = write(buf, content_data, content_data_size);
            } else if (streaming_file) {
                auto start_offset = ftell(streaming_file);
                auto written = fwrite(content_data, content_data_size, 1, streaming_file);
                assert(written == 1 && "write fully");

                rt::array_view_t<uint8_t> buf_range = {};
                buf_range.count = content_data_size;
                buf_range.elements.pos = start_offset;
                rt_fd.data_buffer = buf_range;
            }
            ctx.file_data.push_back(rt_fd);

            ++it_index;
        }

        if (streaming_file) {
            fclose(streaming_file);
        }
    }

    auto store_offset = write_store(buf,
            ctx, groups, events);

    // write root offset finally
    header.store = store_offset;
    write(buf, store_header_offset, &header, 1);

    return buf;
}

}
}
