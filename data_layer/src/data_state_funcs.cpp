#include "data_types.h"

#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

namespace hle_audio {
namespace editor {

struct save_context_t {
    std::vector<flatbuffers::Offset<flatbuffers::String>> sound_files;
    std::vector<flatbuffers::Offset<FileNode>> nodes_file;
    std::vector<flatbuffers::Offset<RandomNode>> nodes_random;
    std::vector<flatbuffers::Offset<SequenceNode>> nodes_sequence;
    std::vector<flatbuffers::Offset<RepeatNode>> nodes_repeat;

    std::unordered_map<std::string_view, uint32_t> sound_files_indices;
};

static uint32_t cache_filename_index(flatbuffers::FlatBufferBuilder& fbb, save_context_t* ctx, const char* filename) {
    auto indices_it = ctx->sound_files_indices.find(filename);
    if (indices_it != ctx->sound_files_indices.end()) {
        return indices_it->second;
    }

    uint32_t index = (uint32_t)ctx->sound_files.size();
    ctx->sound_files_indices[filename] = index;
    ctx->sound_files.push_back(fbb.CreateString(filename));

    return index;
}

static NodeDesc save_node_rec(flatbuffers::FlatBufferBuilder& fbb, save_context_t* ctx, 
        const data_state_t* state, const node_desc_t& desc) {

    auto index = get_index(state->node_ids, desc.id);
    uint16_t out_index = 0;
    switch (desc.type)
    {
    case NodeType_None: {
        break;
    }
    case NodeType_File: {
        auto& file_node = state->nodes_file[index];

        out_index = (uint16_t)ctx->nodes_file.size();
        ctx->nodes_file.push_back(CreateFileNode(fbb,
            cache_filename_index(fbb, ctx, file_node.filename.c_str()),
            file_node.loop,
            file_node.stream
        ));
        break;
    }
    case NodeType_Random: {
        auto& rnd_node = state->nodes_random[index];

        std::vector<hle_audio::NodeDesc> ch_nodes;
        for (auto& ch_desc : rnd_node.nodes) {
            ch_nodes.push_back(save_node_rec(fbb, ctx, state, ch_desc));
        }

        out_index = (uint16_t)ctx->nodes_random.size();
        ctx->nodes_random.push_back(CreateRandomNodeDirect(fbb, &ch_nodes));

        break;
    }
    case NodeType_Sequence: {
        auto& seq_node = state->nodes_sequence[index];

        std::vector<hle_audio::NodeDesc> ch_nodes;
        for (auto& ch_desc : seq_node.nodes) {
            ch_nodes.push_back(save_node_rec(fbb, ctx, state, ch_desc));
        }

        out_index = (uint16_t)ctx->nodes_sequence.size();
        ctx->nodes_sequence.push_back(CreateSequenceNodeDirect(fbb, &ch_nodes));

        break;
    }
    case NodeType_Repeat: {
        auto& rep_node = state->nodes_repeat[index];

        hle_audio::NodeDesc ndesc = save_node_rec(fbb, ctx, state, rep_node.node);

        out_index = (uint16_t)ctx->nodes_repeat.size();
        ctx->nodes_repeat.push_back(CreateRepeatNode(fbb,
            rep_node.repeat_count,
            &ndesc
        ));
        break;
    }
    default:
        assert(false);
        break;
    }

    return NodeDesc(desc.type, out_index);
}

static void save_store_fb_builder(const data_state_t* state, flatbuffers::FlatBufferBuilder& fbb) {
    save_context_t ctx = {};

    std::vector<flatbuffers::Offset<NamedGroup>> groups;
    for (auto& group : state->groups) {
        hle_audio::NodeDesc desc = save_node_rec(fbb, &ctx, state, group.node);

        auto fbo_group = CreateNamedGroupDirect(fbb, 
            group.name.c_str(),
            group.volume,
            group.cross_fade_time,
            group.output_bus_index,
            &desc
        );
        groups.push_back(fbo_group);
    }

    std::vector<flatbuffers::Offset<hle_audio::Event>> events;
    for (auto& ev : state->events) {
        events.push_back(CreateEvent(fbb, &ev));
    }

    fbb.Finish(
        CreateDataStoreDirect(fbb,
            &ctx.sound_files, 
            &ctx.nodes_file, &ctx.nodes_random, &ctx.nodes_sequence, &ctx.nodes_repeat,
            &groups, &events));
}

std::vector<uint8_t> save_store_fb_buffer(const data_state_t* state) {
    flatbuffers::FlatBufferBuilder fbb;
    save_store_fb_builder(state, fbb);

    std::vector<uint8_t> res;
    res.resize(fbb.GetSize());
    memcpy(res.data(), fbb.GetBufferPointer(), fbb.GetSize());

    return res;
}

}
}
