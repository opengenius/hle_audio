#pragma once

#include <vector>

namespace hle_audio {
namespace utils {

enum index_id_t : size_t;
// sparse index array with stacked free index list
using index_id_list_t = std::vector<index_id_t>;

const index_id_t invalid_node_id = (index_id_t)0;

/**
 * Id list
 */
void init(index_id_list_t& id_list);

/**
 * get id ready to be used on next node creation
 */
index_id_t reserve_node_id(index_id_list_t& id_list);
void free_id(index_id_list_t& id_list, index_id_t id);
void store_index(index_id_list_t& id_list, index_id_t id, size_t index);
inline size_t get_index(const index_id_list_t& id_list, index_id_t id) { return (size_t)id_list[id]; }

}
}
