#pragma once

namespace hle_audio {
namespace rt {

static const uint16_t HEAD_INDEX = 0u;

struct index_list_entry_t {
    uint16_t prev;
    uint16_t next;
};

struct index_list_t {
    index_list_entry_t* entries;
};

static void push_back(index_list_t* list, uint16_t index);

/**
 * 
 * @param list 
 * @param entries should have entries_count + 1 size as zero is used as list head
 * @param entries_count 
 */
static void init(index_list_t* list, index_list_entry_t* entries, uint16_t entries_count) {
    list->entries = entries;

    index_list_entry_t head = {};
    entries[HEAD_INDEX] = head;

    for (uint16_t i = 0; i < entries_count; ++i) {
        push_back(list, i);
    }
}

static void push_back(index_list_t* list, uint16_t in_index) {
    uint16_t index = in_index + 1;

    auto head = list->entries[HEAD_INDEX];

    assert(list->entries[head.prev].next == HEAD_INDEX);
    list->entries[head.prev].next = index;

    index_list_entry_t new_entry = {};
    new_entry.prev = head.prev;
    new_entry.next = HEAD_INDEX;
    list->entries[index] = new_entry;

    list->entries[HEAD_INDEX].prev = index;
}

static void erase(index_list_t* list, uint16_t in_index) {
    uint16_t index = in_index + 1;

    auto cur = list->entries[index];
    assert(list->entries[cur.prev].next == index);
    list->entries[cur.prev].next = cur.next;
    assert(list->entries[cur.next].prev == index);
    list->entries[cur.next].prev = cur.prev;
}

static uint16_t pop_front(index_list_t* list) {
    auto head = list->entries[HEAD_INDEX];
    // empty
    if (head.next == HEAD_INDEX) return uint16_t(~0u);

    auto res = head.next - 1;
    erase(list, res);

    return res;
}

}
}
