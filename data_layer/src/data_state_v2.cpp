#include "data_state_v2.h"
#include "data_state_v2_types.h"
#include "data_state_v1.h"
#include "data_state.h"
#include "data_keys.h"

#include "json_utils.inl"

using rapidjson::Document;
using rapidjson::Value;

namespace hle_audio {
namespace data {

namespace v2 {

void init(data_state_t* state);
bool load_state_v2(data_state_t* state, rapidjson::Document& document);

static node_id_t create_node(data_state_t* state, size_t group_index, flow_node_type_t type, vec2_t position) {
    size_t node_index = 0;
    switch (type)
    {
    case FILE_FNODE_TYPE: {
        node_index = state->fnodes_file.add({});
        break;
    }
    case RANDOM_FNODE_TYPE: {
        node_index = state->fnodes_random.add({});
        break;
    }
    default:
        assert(false);
        break;
    }

    common_flow_node_t fnode = {};
    fnode.type = type;
    fnode.position = position;
    fnode.index = node_index;
    auto node_id = node_id_t(state->fnodes.add(fnode));

    auto& group = state->groups[group_index];
    group.nodes.push_back(node_id);

    if (group.start_node == invalid_node_id) {
        group.start_node = node_id;
    }

    return node_id;
}

static common_flow_node_t get_node_data(const data_state_t* state, node_id_t node_id) {
    return state->fnodes[node_id];
}

static const file_flow_node_t& get_file_node(const data_state_t* state, node_id_t id) {
    auto node_index = get_node_data(state, id).index;
    return state->fnodes_file[node_index];
}

static file_flow_node_t& get_file_node_mut(data_state_t* state, node_id_t id) {
    return const_cast<file_flow_node_t&>(get_file_node(state, id));
}

static const random_flow_node_t& get_random_node(const data_state_t* state, node_id_t id) {
    auto node_index = get_node_data(state, id).index;
    return state->fnodes_random[node_index];
}

static random_flow_node_t& get_random_node_mut(data_state_t* state, node_id_t id) {
    return const_cast<random_flow_node_t&>(get_random_node(state, id));
}

static node_id_t load_node(data_state_t* state, const rapidjson::Value& v, size_t group_index) {
    assert(v.IsObject());
    auto node_type = flow_node_type_from_str(v[KEY_TYPE].GetString());
    vec2_t pos = {
        int16_t(v[KEY_POSITION_X].GetInt()),
        int16_t(v[KEY_POSITION_Y].GetInt())
    };

    node_id_t res_id = create_node(state, group_index, node_type, pos);
    switch (node_type)
    {
    case FILE_FNODE_TYPE: {
        auto& node = get_file_node_mut(state, res_id);

        auto& file_v = v["file"];
        node.filename = std::u8string(file_v.GetString(), file_v.GetString() + file_v.GetStringLength());
        node.loop = value_get_opt_bool(v, KEY_LOOP);
        node.stream = value_get_opt_bool(v, KEY_STREAM);

        break;
    }  
    case RANDOM_FNODE_TYPE: {
        auto& node = get_random_node_mut(state, res_id);

        node.out_pin_count = value_get_opt_uint(v, KEY_OUT_COUNT, 1u);

        break;
    }
    default:
        assert(false);
        break;
    }

    return res_id;
}

void init(data_state_t* state) {
    assert(state->fnodes.is_empty());
    auto reserved_invalid_node = state->fnodes.add({});
    assert(reserved_invalid_node == invalid_node_id);

    output_bus_t bus = {};
    bus.name = "Default";
    state->output_buses.push_back(bus);
}

bool load_state_v2(data_state_t* state, Document& document) {
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
        {
            named_group_t group = {};
            group.name = group_v[KEY_NAME].GetString();
            group.volume = value_get_opt_float(group_v, KEY_VOLUME, 1.0f);
            group.cross_fade_time = value_get_opt_float(group_v, KEY_CROSS_FADE_TIME, 0.0f);
            group.output_bus_index = value_get_opt_uint(group_v, KEY_OUTPUT_BUS_INDEX, 0);
            
            state->groups.push_back(group);
        }

        auto group_index = state->groups.size() - 1;
        
        // nodes
        const auto& nodes_val = group_v[KEY_NODES];
        assert(nodes_val.IsArray());
        for (auto& node_v : nodes_val.GetArray()) {
            load_node(state, node_v, group_index);
        }

        auto& group_ref = state->groups[group_index];
        if (group_ref.nodes.size()) {
            group_ref.start_node = group_ref.nodes[group_v[KEY_START].GetUint()];
        }

        // links
        const auto& links_val = group_v[KEY_LINKS];
        assert(links_val.IsArray());
        for (auto& link_v : links_val.GetArray()) {
            link_t l = {};
            l.from.node = group_ref.nodes[link_v[KEY_FROM].GetUint()];
            l.from.pin_index = link_v[KEY_FROM_PIN].GetUint();
            l.to.node = group_ref.nodes[link_v[KEY_TO].GetUint()];
            l.to.pin_index = link_v[KEY_TO_PIN].GetUint();

            group_ref.links.push_back(l);
        }
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

static link_t* find_out_link(named_group_t& group, node_id_t node_id, uint16_t from_pin = 0) {
    for (auto& link : group.links) {
        if (link.from.node == node_id && link.from.pin_index == from_pin) {
            return &link;
        }
    }

    return nullptr;
}

bool migrate_to_v3(data_state_t* state, rapidjson::Document& document) {

    auto doc_version = value_get_opt_uint(document, KEY_VERSION, 1u);

    // 1st version
    v2::data_state_t v2_state = {};
    init(&v2_state);

    if (doc_version == 1) {
        if (!migrate_from_v1_to_v2(&v2_state, document)) return false;
    } else if (!load_state_v2(&v2_state, document)) return false;

    // convert state
    state->events = std::move(v2_state.events);
    state->fnodes = std::move(v2_state.fnodes);
    state->fnodes_file = std::move(v2_state.fnodes_file);
    state->fnodes_random = std::move(v2_state.fnodes_random);
    state->output_buses = std::move(v2_state.output_buses);
    for (auto& group : v2_state.groups) {
        named_group_t new_gr = {};
        new_gr.name = group.name;
        new_gr.volume = group.volume;
        new_gr.output_bus_index = group.output_bus_index;
        new_gr.start_node = group.start_node;
        new_gr.nodes = group.nodes;
        new_gr.links = group.links;

        auto group_index = state->groups.size();

        state->groups.push_back(new_gr);

        // migrate group.cross_fade_time
        if (group.cross_fade_time == 0.0) continue;

        // generate fade filter nodes
        for (auto fnode_id : new_gr.nodes) {
            auto fnode_data = get_node_data(state, fnode_id);
            if (fnode_data.type != FILE_FNODE_TYPE) continue;

            auto& new_gr = state->groups[group_index];
            auto next_link_ptr = find_out_link(new_gr, fnode_id, file_flow_node_t::NEXT_NODE_OUT_PIN);

            auto pos = fnode_data.position;
            pos.x += 10;
            pos.y += 1;
            node_id_t fade_node = create_node(state, group_index, FADE_FNODE_TYPE, pos);
            auto& fade_mut = get_fade_node_mut(state, fade_node);
            fade_mut.start_time = group.cross_fade_time;
            fade_mut.end_time = (next_link_ptr != nullptr) ? group.cross_fade_time : 0.0f;

            if (next_link_ptr) {
                // add delay node with negative cross fade time
                auto pos = fnode_data.position;
                pos.x += 18;
                node_id_t delay_node = create_node(state, group_index, DELAY_FNODE_TYPE, pos);
                auto& delay_mut = get_delay_node_mut(state, delay_node);
                delay_mut.time = -group.cross_fade_time;

                // update file next link to delay node
                auto next_file_attr = next_link_ptr->to;
                next_link_ptr->to.node = delay_node;
                next_link_ptr->to.pin_index = 0;
                
                // link delay node to next node
                link_t l = {};
                l.from.node = delay_node;
                l.from.pin_index = delay_flow_node_t::NEXT_NODE_OUT_PIN;
                l.to = next_file_attr;
                new_gr.links.push_back(l);
            }

            // add filter link
            link_t l = {};
            l.from.node = fnode_id;
            l.from.pin_index = file_flow_node_t::FILTER_OUT_PIN;
            l.to.node = fade_node;
            new_gr.links.push_back(l);

        
        }
    }

    return true;
}

}
}
