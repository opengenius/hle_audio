#pragma once

#include <memory>

namespace hle_audio {
namespace editor {

struct data_state_t;

class cmd_i {
public:
    virtual ~cmd_i() {}
    virtual std::unique_ptr<cmd_i> apply(data_state_t* state) const = 0;
};

}
}
