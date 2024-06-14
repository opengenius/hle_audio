#pragma once

#include "data_types.h"
#include "app_view.h"

namespace hle_audio {
namespace editor {

struct build_node_view_desc_t {
    data::node_id_t node_id;
    bool start_node;
    node_action_data_t* out_data;
};

view_action_type_e build_node_view(const data::file_flow_node_t& file_node, const build_node_view_desc_t& desc);
view_action_type_e build_node_view(const data::random_flow_node_t& node, const build_node_view_desc_t& desc);

}
}
