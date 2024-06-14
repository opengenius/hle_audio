#pragma once

#include <memory>

namespace hle_audio {

namespace data {
    struct data_state_t;
}

namespace editor {

class cmd_i {
public:
    virtual ~cmd_i() {}
    virtual std::unique_ptr<cmd_i> apply(data::data_state_t* state) const = 0;
};

}
}
