#pragma once

namespace hle_audio {
namespace editor {

/**
 * @brief attribute wrapper for unified identification of input/output attributes
 */
struct attribute_id_t {
    data::attribute_t attr;
};

static int pack_attribute_id(const attribute_id_t& attr) {
    static_assert(sizeof(int) == sizeof(int32_t));
    return int(
        uint32_t(attr.attr.node & 0xFFFF) |
        (uint32_t(attr.attr.pin_index) << 16)
    );
}

static attribute_id_t unpack_attribute_id(int attr_id) {
    attribute_id_t res = {};
    res.attr.node = data::node_id_t(attr_id & 0xFFFF);
    res.attr.pin_index = uint16_t(uint32_t(attr_id) >> 16);
    return res;
}

static attribute_id_t in_to_attribute_id(const data::attribute_t& attr) {
    attribute_id_t res = {};
    res.attr = attr;
    return res;
}

static attribute_id_t out_to_attribute_id(uint16_t in_count, const data::attribute_t& attr) {
    attribute_id_t res = {};
    res.attr = attr;
    res.attr.pin_index += in_count;
    return res;
}

static data::attribute_t attribute_id_to_out(uint16_t in_count, const attribute_id_t& attr) {
    data::attribute_t res = attr.attr;
    res.pin_index -= in_count;
    return res;
}

static data::attribute_t attribute_id_to_in(const attribute_id_t& attr) {
    data::attribute_t res = attr.attr;
    return res;
}

} // namespace editor
}
