#pragma once

namespace hle_audio {
namespace rt {

struct index_list_entry_t {
    uint16_t prev;
    uint16_t next;
};
struct index_list_t {
    index_list_entry_t* entries;
    uint16_t head_index;
};

static void push_back(index_list_t* list, uint16_t index);

static void init(index_list_t* list, index_list_entry_t* entries, uint16_t head_index) {
    list->entries = entries;
    list->head_index = head_index;

    index_list_entry_t head = {};
    head.prev = head.next = head_index;
    entries[head_index] = head;

    for (uint16_t i = 0; i < head_index; ++i) {
        push_back(list, i);
    }
}

static void push_back(index_list_t* list, uint16_t index) {
    auto head = list->entries[list->head_index];

    assert(list->entries[head.prev].next == list->head_index);
    list->entries[head.prev].next = index;

    index_list_entry_t new_entry = {};
    new_entry.prev = head.prev;
    new_entry.next = list->head_index;
    list->entries[index] = new_entry;

    list->entries[list->head_index].prev = index;
}

static void erase(index_list_t* list, uint16_t index) {
    auto cur = list->entries[index];
    assert(list->entries[cur.prev].next == index);
    list->entries[cur.prev].next = cur.next;
    assert(list->entries[cur.next].prev == index);
    list->entries[cur.next].prev = cur.prev;
}

static uint16_t pop_front(index_list_t* list) {
    auto head = list->entries[list->head_index];
    // empty
    if (head.next == list->head_index) return ~0u;

    erase(list, head.next);

    return head.next;
}

}
}
