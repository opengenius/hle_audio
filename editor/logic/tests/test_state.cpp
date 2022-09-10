#include "gtest/gtest.h"
#include "app_logic.h"

using namespace hle_audio;
using namespace hle_audio::editor;

TEST(logic_state, remove_node_with_children)
{
    logic_state_t state = {};

    init(&state);

    // children nodes have indices less than parent node case

    node_desc_t desc_0 = {
        NodeType_Sequence,
        reserve_node_id(state.data_state.node_ids)
    };
    create_node(&state.data_state, desc_0);
    node_desc_t desc_1 = {
        NodeType_Sequence,
        reserve_node_id(state.data_state.node_ids)
    };
    create_node(&state.data_state, desc_1);

    create_group(&state, 0);
    create_root_node(&state, 0, NodeType_Sequence);

    auto& group_ref = get_group(&state.data_state, 0);
    auto nodes_ptr = get_child_nodes_ptr_mut(&state.data_state, group_ref.node);
    nodes_ptr->push_back(desc_0);
    nodes_ptr->push_back(desc_1);
    
    // clear group nodes
    remove_root_node(&state, 0);

    ASSERT_TRUE(state.data_state.nodes_sequence.is_empty());
}
