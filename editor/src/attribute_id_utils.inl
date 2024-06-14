#pragma once

namespace hle_audio {
namespace editor {

static int to_attribute_id_in(data::node_id_t node_id, uint16_t pin) {
    static_assert(sizeof(int) == sizeof(int32_t));
    return int(
        uint32_t(node_id & 0xFFFF) |
        (uint32_t(pin) << 16)
    );
}

static int to_attribute_id_out(uint16_t in_count, data::node_id_t node_id, uint16_t pin) {
    static_assert(sizeof(int) == sizeof(int32_t));

    uint32_t high_bits = uint32_t(in_count + pin) << 16;
    uint32_t packed_v = uint32_t(node_id & 0xFFFF) | high_bits;
    return int(packed_v);
}

}
}
