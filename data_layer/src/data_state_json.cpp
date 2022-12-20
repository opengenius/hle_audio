#include "data_types.h"

#include <cmath>

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/document.h"

// using rapidjson::StringBuffer;
using rapidjson::FileWriteStream;
using rapidjson::PrettyWriter;
using rapidjson::Document;
using rapidjson::Value;

namespace hle_audio {
namespace editor {

const auto KEY_NAME = "name";
const auto KEY_LOOP = "loop";
const auto KEY_STREAM = "stream";
const auto KEY_ACTIONS = "actions";
const auto KEY_TYPE = "type";
const auto KEY_TARGET_GROUP_INDEX = "target_group_index";
const auto KEY_FADE_TIME = "fade_time";
const auto KEY_TIMES = "times";
const auto KEY_CROSS_FADE_TIME = "cross_fade_time";
const auto KEY_OUTPUT_BUS_INDEX = "output_bus_index";

static rt::node_type_e node_type_from_str(const char* str) {
    int i = 0;
    for (auto name : rt::c_node_type_names) {
        if (strcmp(name, str) == 0) {
            return (rt::node_type_e)i;
        }
        ++i;
    }
    return rt::node_type_e::None;
}

static rt::action_type_e ActionType_from_str(const char* str) {
    int i = 0;
    for (auto name : rt::c_action_type_names) {
        if (strcmp(name, str) == 0) {
            return (rt::action_type_e)i;
        }
        ++i;
    }
    return rt::action_type_e::none;
}

static bool value_get_opt_bool(const Value& v, const char* key, bool def_value = false) {
    bool res = def_value;
    if (v.HasMember(key)) {
        res = v[key].GetBool();
    }
    return res;
}

static float value_get_opt_float(const Value& v, const char* key, float def_value = 0.0f) {
    float res = def_value;
    if (v.HasMember(key)) {
        res = v[key].GetFloat();
    }
    return res;
}

static unsigned value_get_opt_uint(const Value& v, const char* key, unsigned def_value = 0) {
    unsigned res = def_value;
    if (v.HasMember(key)) {
        res = v[key].GetUint();
    }
    return res;
}

static node_desc_t load_node_rec(data_state_t* state, const Value& v) {
    assert(v.IsObject());
    auto node_type = node_type_from_str(v[KEY_TYPE].GetString());
    node_desc_t res = {
        node_type,
        reserve_node_id(state->node_ids)
    };
    create_node(state, res);
    switch (node_type)
    {
    case rt::node_type_e::None: {
        break;
    }
    case rt::node_type_e::File: {
        auto& node = get_file_node_mut(state, res.id);
        node.filename = v["file"].GetString();
        node.loop = value_get_opt_bool(v, KEY_LOOP);
        node.stream = value_get_opt_bool(v, KEY_STREAM);

        break;
    }  
    case rt::node_type_e::Random:
    case rt::node_type_e::Sequence: {
        const auto& nodes_val = v["nodes"];
        assert(nodes_val.IsArray());
        for (auto& node_v : nodes_val.GetArray()) {
            auto ch_desc = load_node_rec(state, node_v);
            get_child_nodes_ptr_mut(state, res)->push_back(ch_desc);
        }
        
        break;
    }
    case rt::node_type_e::Repeat: {
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
    const auto& groups_val = document["groups"];
    assert(groups_val.IsArray());
    for (auto& group_v : groups_val.GetArray()) {
        named_group_t group = {};

        group.name = group_v[KEY_NAME].GetString();
        group.volume = value_get_opt_float(group_v, "volume", 1.0f);
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
            action.type = ActionType_from_str(action_v[KEY_TYPE].GetString());
            action.target_index = action_v[KEY_TARGET_GROUP_INDEX].GetUint();
            action.fade_time = value_get_opt_float(action_v, KEY_FADE_TIME);

            event.actions.push_back(action);
        }

        state->events.push_back(event);
    }

    return true;
}

template <typename Writer>
static void write_node_rec(Writer& writer,
        const data_state_t* state, const node_desc_t& desc) {
    writer.StartObject();

    writer.String(KEY_TYPE);
    writer.String(node_type_name(desc.type));
    switch (desc.type)
    {
    case rt::node_type_e::File: {
        auto& file_node = get_file_node(state, desc.id);
        writer.String("file");
        writer.String(file_node.filename);

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
    case rt::node_type_e::Repeat: {
        auto& node = get_repeat_node(state, desc.id);
        writer.String("node");
        write_node_rec(writer, state, node.node);
        if (node.repeat_count) {
            writer.String(KEY_TIMES);
            writer.Uint(node.repeat_count);
        }
        break;
    }
    default:
        break;
    }

    if (auto nodes_ptr = get_child_nodes_ptr(state, desc)) {
        writer.String("nodes");
        writer.StartArray();
        for (auto& ch_desc : *nodes_ptr) {
            write_node_rec(writer, state, ch_desc);
        }
        writer.EndArray();
    }

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
    writer.String("groups");
    writer.StartArray();
    for (auto& group : state->groups) {
        writer.StartObject();

        // name:string;
        writer.String(KEY_NAME);
        writer.String(group.name);

        // volume:float = 1.0;
        if (group.volume < 1.0f) {
            writer.String("volume");
            
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
        
        writer.String("node");
        write_node_rec(writer, state, group.node);
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
