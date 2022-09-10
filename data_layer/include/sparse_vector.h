#pragma once

#include <vector>

namespace hle_audio {
namespace utils {

template<typename T>
struct sparse_vector {
    std::vector<T> data;
    std::vector<size_t> free_list;

    size_t add(const T& el) {
        size_t index;
        if (free_list.size()) {
            index = free_list.back();
            free_list.pop_back();
            data[index] = el;
        } else {
            index = data.size();
            data.push_back(el);
        }
        return index;
    }

    void remove(size_t index) {
        free_list.push_back(index);
    }

    T& operator[](size_t index) {
        return data[index];
    }

    const T& operator[](size_t index) const {
        return data[index];
    }

    const bool is_empty() const {
        return free_list.size() == data.size();
    }
};

}
}
