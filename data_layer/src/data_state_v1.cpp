#include "data_state_v1.h"
#include "data_keys.h"
#include "data_state_v2_types.h"

#include "json_utils.inl"

using rapidjson::Document;
using rapidjson::Value;

namespace hle_audio {
namespace data {

namespace v1 {
    
enum class node_type_e : uint8_t {
    None,
    File,
    Random,
    Sequence,
    Repeat
};

static const char* const c_node_type_names[] = {
    "None",
    "File",
    "Random",
    "Sequence",
    "Repeat"
};

static const char* node_type_name(node_type_e type) {
    return c_node_type_names[(size_t)type];
}

struct node_desc_t {
    node_type_e type;
    utils::index_id_t id;
};

struct named_group_t {
    std::string name = {};
    float volume = 1.0f;
    float cross_fade_time = 0.0f;
    uint8_t output_bus_index = 0;
    node_desc_t node = {};
};

struct file_node_t {
    std::u8string filename;
    bool loop = false;
    bool stream = false;
};

struct node_random_t {
    std::vector<node_desc_t> nodes;
};

struct node_sequence_t {
    std::vector<node_desc_t> nodes;
};

struct node_repeat_t {
    uint16_t repeat_count;
    node_desc_t node;
};

struct data_state_t {
    utils::index_id_list_t node_ids;

    utils::sparse_vector<file_node_t> nodes_file;
    utils::sparse_vector<node_random_t> nodes_random;
    utils::sparse_vector<node_sequence_t> nodes_sequence;
    utils::sparse_vector<node_repeat_t> nodes_repeat;

    std::vector<named_group_t> groups;
    std::vector<event_t> events;

    std::vector<output_bus_t> output_buses;
};

void init(data_state_t* state);
bool load_state_v1(data_state_t* state, rapidjson::Document& document);

const file_node_t& get_file_node(const data_state_t* state, utils::index_id_t id);
const std::vector<node_desc_t>* get_child_nodes_ptr(const data_state_t* state, const node_desc_t& node);
const node_repeat_t& get_repeat_node(const data_state_t* state, utils::index_id_t id);

static node_desc_t create_node(data_state_t* state, node_type_e type) {
    size_t node_index = 0;
    switch (type)
    {
    case node_type_e::None: {
        break;
    }
    case node_type_e::File: {
        node_index = state->nodes_file.add({});
        break;
    }
    case node_type_e::Random: {
        node_index = state->nodes_random.add({});
        break;
    }
    case node_type_e::Sequence: {
        node_index = state->nodes_sequence.add({});
        break;
    }
    case node_type_e::Repeat: {
        node_index = state->nodes_repeat.add({});
        break;
    }
    default:
        assert(false);
        break;
    }

    node_desc_t desc = {};
    desc.type = type;
    desc.id = alloc_id(state->node_ids);
    store_index(state->node_ids, desc.id, node_index);

    return desc;
}

static node_type_e node_type_from_str(const char* str) {
    int i = 0;
    for (auto name : c_node_type_names) {
        if (strcmp(name, str) == 0) {
            return node_type_e(i);
        }
        ++i;
    }
    return node_type_e::None;
}

const file_node_t& get_file_node(const data_state_t* state, utils::index_id_t id) {
    auto node_index = get_index(state->node_ids, id);
    return state->nodes_file[node_index];
}

static file_node_t& get_file_node_mut(data_state_t* state, utils::index_id_t id) {
    return const_cast<file_node_t&>(get_file_node(state, id));
}

const node_repeat_t& get_repeat_node(const data_state_t* state, utils::index_id_t id) {
    auto node_index = get_index(state->node_ids, id);
    return state->nodes_repeat[node_index];
}

node_repeat_t& get_repeat_node_mut(data_state_t* state, utils::index_id_t id) {
    return const_cast<node_repeat_t&>(get_repeat_node(state, id));
}

const std::vector<node_desc_t>* get_child_nodes_ptr(const data_state_t* state, const node_desc_t& node) {
    auto node_index = get_index(state->node_ids, node.id);

    switch (node.type)
    {       
    case node_type_e::Random:
        return &state->nodes_random[node_index].nodes;
    case node_type_e::Sequence:
        return &state->nodes_sequence[node_index].nodes;
    default:
        // no child nodes
        break;
    }
    return nullptr;
}

static std::vector<node_desc_t>* get_child_nodes_ptr_mut(data_state_t* state, const node_desc_t& node) {
    return const_cast<std::vector<node_desc_t>*>(get_child_nodes_ptr(state, node));
}

static node_desc_t load_node_rec(data_state_t* state, const rapidjson::Value& v) {
    assert(v.IsObject());
    auto node_type = node_type_from_str(v[KEY_TYPE].GetString());
    node_desc_t res = create_node(state, node_type);
    switch (node_type)
    {
    case node_type_e::None: {
        break;
    }
    case node_type_e::File: {
        auto& node = get_file_node_mut(state, res.id);
        auto& file_v = v["file"];
        node.filename = std::u8string(file_v.GetString(), file_v.GetString() + file_v.GetStringLength());
        node.loop = value_get_opt_bool(v, KEY_LOOP);
        node.stream = value_get_opt_bool(v, KEY_STREAM);

        break;
    }  
    case node_type_e::Random:
    case node_type_e::Sequence: {
        const auto& nodes_val = v["nodes"];
        assert(nodes_val.IsArray());
        for (auto& node_v : nodes_val.GetArray()) {
            auto ch_desc = load_node_rec(state, node_v);
            get_child_nodes_ptr_mut(state, res)->push_back(ch_desc);
        }
        
        break;
    }
    case node_type_e::Repeat: {
        auto& node = get_repeat_node_mut(state, res.id);
        node.repeat_count = value_get_opt_uint(v, KEY_TIMES);
        node.node = load_node_rec(state, v["node"]);

        break;
    }
    default:
        assert(false);
        break;
    }

    return res;
}

void init(data_state_t* state) {
    init(state->node_ids);

    output_bus_t bus = {};
    bus.name = "Default";
    state->output_buses.push_back(bus);
}

bool load_state_v1(data_state_t* state, Document& document) {
    // output buses
    auto KEY_OUTPUT_BUSES = "output_buses";
    if (document.HasMember(KEY_OUTPUT_BUSES)) {
        const auto& output_buses_val = document[KEY_OUTPUT_BUSES];
        if (output_buses_val.IsArray()) {
            state->output_buses.clear();
            for (auto& output_bus_v : output_buses_val.GetArray()) {
                output_bus_t bus = {};
                bus.name = output_bus_v[KEY_NAME].GetString();
                
                state->output_buses.push_back(bus);
            }
        }
    }

    // groups
    const auto& groups_val = document[KEY_GROUPS];
    assert(groups_val.IsArray());
    for (auto& group_v : groups_val.GetArray()) {
        v1::named_group_t group = {};

        group.name = group_v[KEY_NAME].GetString();
        group.volume = value_get_opt_float(group_v, KEY_VOLUME, 1.0f);
        group.cross_fade_time = value_get_opt_float(group_v, KEY_CROSS_FADE_TIME, 0.0f);
        group.output_bus_index = value_get_opt_uint(group_v, KEY_OUTPUT_BUS_INDEX, 0);
        group.node = load_node_rec(state, group_v["node"]); 
        
        state->groups.push_back(group);
    }

    // events
    const auto& events_v = document["events"];
    assert(events_v.IsArray());
    for (auto& event_v : events_v.GetArray()) {
        event_t event = {};
        event.name = event_v[KEY_NAME].GetString();

        const auto& actions_v = event_v[KEY_ACTIONS];
        assert(actions_v.IsArray());
        for (auto& action_v : actions_v.GetArray()) {
            rt::action_t action = {};
            action.type = rt::action_type_from_str(action_v[KEY_TYPE].GetString());
            action.target_index = action_v[KEY_TARGET_GROUP_INDEX].GetUint();
            action.fade_time = value_get_opt_float(action_v, KEY_FADE_TIME);

            event.actions.push_back(action);
        }

        state->events.push_back(event);
    }

    return true;
}

} // namespace v1

typedef struct {
    v2::data_state_t* state;
    v2::named_group_t* new_gr;
    const v1::data_state_t* v1_state;
} add_group_nodes_ctx_t;

/*
    migration notes:

    unsupported case:
    repeat
        seq
            rnd
                f1
                f2
            rnd
                f3
                f4

    - sequence  supports only sequence of files
    - no support of repeat for sequence
*/
static node_id_t add_group_nodes_rec(const add_group_nodes_ctx_t& ctx, 
                v1::node_desc_t desc,
                float x_offset, float y_offset,
                float* out_width, float* out_height,
                bool link_last_with_first = false, node_id_t node_to_link_last_to = invalid_node_id) {
    const float node_w = 8.0f;
    const float node_h = 4.0f;

    float full_h = node_h;
    float full_w = node_w;
    node_id_t first_node_id = invalid_node_id;

    if (desc.type == v1::node_type_e::File) {
        auto& file_node = get_file_node(ctx.v1_state, desc.id);

        common_flow_node_t fnode = {};
        node_id_t fnode_id = node_id_t(ctx.state->fnodes.add(fnode)); // reserve id
        ctx.new_gr->nodes.push_back(fnode_id);

        file_flow_node_t node = {};
        node.filename = file_node.filename;
        node.stream = file_node.stream;
        node.loop = file_node.loop;
        
        fnode.type = FILE_FNODE_TYPE;
        fnode.index = ctx.state->fnodes_file.add(node);
        fnode.position.x = x_offset;
        fnode.position.y = y_offset;
        // fill node data
        ctx.state->fnodes[fnode_id] = fnode;

        first_node_id = fnode_id;

        if (link_last_with_first) {
            link_t l = {};
            l.from.node = fnode_id;
            l.from.pin_index = 0;
            l.to.node = (node_to_link_last_to == invalid_node_id) ? fnode_id : node_to_link_last_to;
            l.to.pin_index = 0;
            ctx.new_gr->links.push_back(l);
        }

    } else if (desc.type == v1::node_type_e::Random) {

        common_flow_node_t fnode = {};
        node_id_t fnode_id = node_id_t(ctx.state->fnodes.add(fnode)); // reserve id
        ctx.new_gr->nodes.push_back(fnode_id);

        if (link_last_with_first && node_to_link_last_to == invalid_node_id) {
            node_to_link_last_to = fnode_id;
        }

        random_flow_node_t node = {};

        if (auto nodes_ptr = get_child_nodes_ptr(ctx.v1_state, desc)) {
            node.out_pin_count = nodes_ptr->size();

            float ch_y_offset = y_offset;
            for (auto& ch_desc : *nodes_ptr) {
                auto ch_index = &ch_desc - nodes_ptr->data();

                float ch_height = 0.0f;
                float ch_width = 0.0f;
                auto ch_node_id = add_group_nodes_rec(ctx, ch_desc, x_offset + node_w, ch_y_offset, &ch_width, &ch_height, link_last_with_first, node_to_link_last_to);

                link_t l = {};
                l.from.node = fnode_id;
                l.from.pin_index = ch_index;
                l.to.node = ch_node_id;
                l.to.pin_index = 0;
                ctx.new_gr->links.push_back(l);

                ch_y_offset += ch_height;
            }

            full_h = ch_y_offset;
        }

        fnode.type = RANDOM_FNODE_TYPE;
        fnode.index = ctx.state->fnodes_random.add(node);
        fnode.position.x = x_offset;
        fnode.position.y = y_offset + (full_h - node_h) * 0.5f;
        // fill node data
        ctx.state->fnodes[fnode_id] = fnode;

        first_node_id = fnode_id;
    } else if (desc.type == v1::node_type_e::Sequence) {
        if (auto nodes_ptr = get_child_nodes_ptr(ctx.v1_state, desc)) {

            node_id_t prev_it_node = invalid_node_id;
            float ch_x_offset = x_offset;
            for (auto& ch_desc : *nodes_ptr) {
                auto ch_index = &ch_desc - nodes_ptr->data();

                assert(ch_desc.type == v1::node_type_e::File);
                if (ch_desc.type != v1::node_type_e::File) continue;

                float ch_width = 0.0f;
                float ch_height = 0.0f;
                auto ch_node_id = add_group_nodes_rec(ctx, ch_desc, ch_x_offset, y_offset, &ch_width, &ch_height);
                
                // keep first node for result
                if (ch_index == 0) {
                    first_node_id = ch_node_id;
                } else {
                    link_t l = {};
                    l.from.node = prev_it_node;
                    l.from.pin_index = 0;
                    l.to.node = ch_node_id;
                    l.to.pin_index = 0;
                    ctx.new_gr->links.push_back(l);
                }

                prev_it_node = ch_node_id;
                ch_x_offset += ch_width;
            }

            full_w = ch_x_offset;
        }
    } else if (desc.type == v1::node_type_e::Repeat) {
        auto& repeat_node = get_repeat_node(ctx.v1_state, desc.id);

        float ch_height = 0.0f;
        float ch_width = 0.0f;
        auto ch_node_id = add_group_nodes_rec(ctx, repeat_node.node, x_offset, y_offset, &ch_width, &ch_height, true);
        first_node_id = ch_node_id;
        full_w = ch_width;
        full_h = ch_height;
    }

    *out_height = full_h;
    *out_width = full_w;
    return first_node_id;
}

bool migrate_from_v1_to_v2(v2::data_state_t* state, rapidjson::Document& document) {
    // 1st vertsion
    v1::data_state_t v1_state = {};
    init(&v1_state);
    if (!load_state_v1(&v1_state, document)) return false;

    // convert state
    for (auto& group : v1_state.groups) {
        v2::named_group_t new_gr = {};
        new_gr.name = group.name;
        new_gr.volume = group.volume;
        new_gr.cross_fade_time = group.cross_fade_time;
        new_gr.output_bus_index = group.output_bus_index;


        add_group_nodes_ctx_t ctx = {};
        ctx.state = state;
        ctx.new_gr = &new_gr;
        ctx.v1_state = &v1_state;

        float width = 0.0f, height = 0.0f;
        add_group_nodes_rec(ctx, group.node, 1.0f, 0.0f, &width, &height);
        new_gr.start_node = new_gr.nodes.size() ? new_gr.nodes[0] : invalid_node_id;

        state->groups.push_back(new_gr);
    }
    state->events = std::move(v1_state.events);
    state->output_buses = std::move(v1_state.output_buses);

    return true;
}

}
}
