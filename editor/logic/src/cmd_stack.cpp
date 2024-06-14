#include "cmd_stack.h"
#include <cassert>

using hle_audio::data::data_state_t;

namespace hle_audio {
namespace editor {

bool has_undo(const command_stack_t* cmds) {
    return cmds->undo_stack.size() > 0;
}

bool has_redo(const command_stack_t* cmds) {
    return cmds->redo_stack.size() > 0;
}

void push_chain_start(command_stack_t* cmds) {
    cmds->undo_stack.push_back({nullptr});
    cmds->redo_stack.clear();
}

void execute_cmd(command_stack_t* cmds, data_state_t* data_state, std::unique_ptr<cmd_i> cmd) {
    assert(cmds->redo_stack.size() == 0);

    auto reverse_cmd = cmd->apply(data_state);
    cmds->undo_stack.push_back(std::move(reverse_cmd));
}

template<typename T>
T return_back_with_pop(std::vector<T>& vec) {
    T res = std::move(vec.back());
    vec.pop_back();
    return res;
}

static void _apply_cmd_chain(data_state_t* state, 
                std::vector<std::unique_ptr<cmd_i>>& src_stack, 
                std::vector<std::unique_ptr<cmd_i>>& dst_stack) {
    assert(dst_stack.size() == 0 || dst_stack.back() != nullptr);

    dst_stack.push_back(nullptr);

    

    while(auto back_slot = return_back_with_pop(src_stack)) {
        auto reverse_cmd = back_slot->apply(state);

        dst_stack.push_back(std::move(reverse_cmd));
    }
}

bool apply_undo_chain(command_stack_t* cmds, data_state_t* data_state) {
    if (!has_undo(cmds)) return false;

    _apply_cmd_chain(data_state, cmds->undo_stack, cmds->redo_stack);

    return true;
}

bool apply_redo_chain(command_stack_t* cmds, data_state_t* data_state) {
    if (!has_redo(cmds)) return false;

    _apply_cmd_chain(data_state, cmds->redo_stack, cmds->undo_stack);

    return true;
}

}
}
