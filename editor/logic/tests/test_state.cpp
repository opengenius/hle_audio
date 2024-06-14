#include "gtest/gtest.h"
#include "app_logic.h"
#include "data_state.h"

using namespace hle_audio;
using namespace hle_audio::editor;

TEST(logic_state, remove_group_with_nodes)
{
    logic_state_t state = {};
    init(&state);

    create_group(&state, 0);
    create_node(&state, 0, data::RANDOM_FNODE_TYPE, {});
    create_node(&state, 0, data::FILE_FNODE_TYPE, {});
    remove_group(&state, 0);

    ASSERT_TRUE(state.data_state.fnodes_file.is_empty());
    ASSERT_TRUE(state.data_state.fnodes_random.is_empty());
}

TEST(logic_state, remove_group_and_undo_start_node)
{
    logic_state_t state = {};
    init(&state);

    create_group(&state, 0);
    create_node(&state, 0, data::RANDOM_FNODE_TYPE, {});
    create_node(&state, 0, data::FILE_FNODE_TYPE, {});

    // fisrt node should be the start node
    auto start_node = get_group(&state.data_state, 0).start_node;
    ASSERT_TRUE(start_node != data::invalid_node_id);
    auto node_data = get_node_data(&state.data_state, start_node);
    ASSERT_TRUE(node_data.type == data::RANDOM_FNODE_TYPE);

    remove_group(&state, 0);
    apply_undo_chain(&state.cmds, &state.data_state);

    // start node must be the same after undo
    auto start_node_after_undo = get_group(&state.data_state, 0).start_node;
    ASSERT_TRUE(start_node_after_undo == start_node);
}
