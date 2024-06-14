#pragma once

#include "cmd_types.h"
#include <vector>

namespace hle_audio {
namespace editor {

struct command_stack_t {
    std::vector<std::unique_ptr<cmd_i>> undo_stack;
    std::vector<std::unique_ptr<cmd_i>> redo_stack;
};

bool has_undo(const command_stack_t* cmds);
bool has_redo(const command_stack_t* cmds);

void push_chain_start(command_stack_t* cmds);
void execute_cmd(command_stack_t* cmds, data::data_state_t* data_state, std::unique_ptr<cmd_i> cmd);

bool apply_undo_chain(command_stack_t* cmds, data::data_state_t* data_state);
bool apply_redo_chain(command_stack_t* cmds, data::data_state_t* data_state);

inline size_t get_undo_size(const command_stack_t* cmds) { return cmds->undo_stack.size(); }

}
}
