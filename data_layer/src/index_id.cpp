#include "index_id.h"
#include <cassert>
#include <limits>

namespace hle_audio {
namespace utils {
    
const size_t free_list_index = 0u;

void init(index_id_list_t& id_list) {
    id_list.resize(1); // 0 is freelist head
}

void free_id(index_id_list_t& id_list, index_id_t id) {
    id_list[id] = id_list[free_list_index];
    id_list[free_list_index] = id;
}

index_id_t alloc_id(index_id_list_t& id_list) {
    index_id_t res;
    if (id_list[free_list_index] != invalid_node_id) {
        res = id_list[free_list_index];
        id_list[free_list_index] = id_list[res];
    } else {
        res = (index_id_t)id_list.size();
        id_list.push_back(invalid_node_id);
    }
    return res;
}

void store_index(index_id_list_t& id_list, index_id_t id, size_t index) {
    // store index
    assert(index < std::numeric_limits<std::underlying_type_t<index_id_t>>::max());
    id_list[id] = (index_id_t)index;
}

}
}
