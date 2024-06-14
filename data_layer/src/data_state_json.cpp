#include "data_types.h"

#include <cmath>

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/document.h"

#include "data_state_v1.h"
#include "data_state.h"
#include "data_keys.h"
#include "json_utils.inl"

// using rapidjson::StringBuffer;
using rapidjson::FileWriteStream;
using rapidjson::PrettyWriter;
using rapidjson::Document;
using rapidjson::Value;

static const uint32_t CURRENT_VERSION = 2;

namespace hle_audio {
namespace data {

static node_id_t load_node(data_state_t* state, const rapidjson::Value& v, size_t group_index) {
    assert(v.IsObject());
    auto node_type = flow_node_type_from_str(v[KEY_TYPE].GetString());
    vec2_t pos = {
        v[KEY_POSITION_X].GetInt(),
        v[KEY_POSITION_Y].GetInt()
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

bool load_store_json(data_state_t* state, const char* json_filename) {
    FILE* fp = fopen(json_filename, "rb");
    if (!fp) return false;

    // get file size
    fseek(fp, 0L, SEEK_END);
    auto file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    std::vector<char> json_buf(file_size);

    // read json data
    fread(json_buf.data(), 1, file_size, fp);
    fclose(fp);

    // todo: reset data state
    assert(state->groups.size() == 0);

    Document document;
    document.Parse(json_buf.data(), json_buf.size());

    auto doc_version = value_get_opt_uint(document, KEY_VERSION, 1u);
    if (doc_version == 1) {
        return migrate_from_v1(state, document);
    } else {
        //
        // load current version
        //
        assert(doc_version == CURRENT_VERSION);

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
                l.from = group_ref.nodes[link_v[KEY_FROM].GetUint()];
                l.from_pin = link_v[KEY_FROM_PIN].GetUint();
                l.to = group_ref.nodes[link_v[KEY_TO].GetUint()];
                l.to_pin = link_v[KEY_TO_PIN].GetUint();

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
    }


    return true;
}

static void write_node(PrettyWriter<FileWriteStream>& writer,
        const data_state_t* state, const node_id_t& node_id) {

    auto& node = state->fnodes[node_id];

    writer.StartObject();

    writer.String(KEY_TYPE);
    writer.String(flow_node_type_name(node.type));

    writer.String(KEY_POSITION_X);
    writer.Int(node.position.x);
    writer.String(KEY_POSITION_Y);
    writer.Int(node.position.y);

    switch (node.type)
    {
    case FILE_FNODE_TYPE: {
        auto& file_node = get_file_node(state, node_id);
        writer.String("file");
        writer.String((const char*)file_node.filename.c_str(), file_node.filename.length());

        if (file_node.loop) {
            writer.String(KEY_LOOP);
            writer.Bool(file_node.loop);
        }
        if (file_node.stream) {
            writer.String(KEY_STREAM);
            writer.Bool(file_node.stream);
        }
        break;
    }
    case RANDOM_FNODE_TYPE: {
        auto& random_node = get_random_node(state, node_id);
        writer.String(KEY_OUT_COUNT);
        writer.Uint(random_node.out_pin_count);
        break;
    }

    default:
        assert(false && "unhandled type");
        break;
    }

    writer.EndObject();
}

static uint16_t node_in_group_index(const named_group_t& group, node_id_t node_id) {
    for (auto& it_node_id : group.nodes) {
        if (it_node_id == node_id) {
            auto it_index = &it_node_id - group.nodes.data();
            return it_index;
        }
    }

    assert(false && "no node found, invalid link!");
    return ~0u;
}

static void write_link(PrettyWriter<FileWriteStream>& writer,
        const data_state_t* state, const named_group_t& group, const link_t& link) {
    writer.StartObject();

    writer.String(KEY_FROM);
    writer.Uint(node_in_group_index(group, link.from));
    writer.String(KEY_FROM_PIN);
    writer.Uint(link.from_pin);
    
    writer.String(KEY_TO);
    writer.Uint(node_in_group_index(group, link.to));
    writer.String(KEY_TO_PIN);
    writer.Uint(link.to_pin);

    writer.EndObject();
}

void save_store_json(const data_state_t* state, const char* json_filename) {
    FILE* fp = fopen(json_filename, "wb");
    if (!fp) return;
 
    char writeBuffer[65536];
    FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
    PrettyWriter<FileWriteStream> writer(os);

    // store
    writer.StartObject();

    writer.String(KEY_VERSION);
    writer.Uint(CURRENT_VERSION);

    // output buses
    writer.String("output_buses");
    writer.StartArray();
    for (auto& output_bus : state->output_buses) {
        writer.StartObject();

        writer.String(KEY_NAME);
        writer.String(output_bus.name);

        writer.EndObject();
    }
    writer.EndArray();

    // groups
    writer.String(KEY_GROUPS);
    writer.StartArray();
    for (auto& group : state->groups) {
        writer.StartObject();

        // name:string;
        writer.String(KEY_NAME);
        writer.String(group.name);

        // volume:float = 1.0;
        if (group.volume < 1.0f) {
            writer.String(KEY_VOLUME);
            
            double volume = group.volume;
            volume = round(volume * 1000) / 1000;
            writer.Double(volume);

            // writer.SetMaxDecimalPlaces(3);
            // writer.Double(group.volume);
        }

        if (group.cross_fade_time) {
            writer.String(KEY_CROSS_FADE_TIME);

            double fade_time = group.cross_fade_time;
            fade_time = round(fade_time * 1000) / 1000;
            writer.Double(fade_time);
        }

        if (group.output_bus_index) {
            writer.String(KEY_OUTPUT_BUS_INDEX);
            writer.Uint(group.output_bus_index);
        }

        if (group.nodes.size()) {
            writer.String(KEY_START);
            writer.Uint(node_in_group_index(group, group.start_node));
        }
        
        // nodes
        writer.String(KEY_NODES);
        writer.StartArray();
        for (auto& node_id : group.nodes) {
            write_node(writer, state, node_id);
        }
        writer.EndArray();

        // links
        writer.String(KEY_LINKS);
        writer.StartArray();
        for (auto& link : group.links) {
            write_link(writer, state, group, link);
        }
        writer.EndArray();
        
        writer.EndObject();
    }
    writer.EndArray();

    // events
    writer.String("events");
    writer.StartArray();
    for (auto& event : state->events) {
        writer.StartObject();

        // name:string;
        writer.String(KEY_NAME);
        writer.String(event.name);
        
        writer.String(KEY_ACTIONS);
        writer.StartArray();
        for (auto& action : event.actions) {
            writer.StartObject();

            writer.String(KEY_TYPE);
            writer.String(action_type_name(action.type));

            writer.String(KEY_TARGET_GROUP_INDEX);
            writer.Uint(action.target_index);
            
            if (action.fade_time > 0.0f) {
                writer.String(KEY_FADE_TIME);

                double fade_time = action.fade_time;
                fade_time = round(fade_time * 1000) / 1000;
                writer.Double(fade_time);
                // writer.Double(action->fade_time);
            }

            writer.EndObject();
        }
        writer.EndArray();

        writer.EndObject();
    }
    writer.EndArray();

    writer.EndObject();

    fclose(fp);
}

}
}
