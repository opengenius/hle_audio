#include "data_types.h"

using namespace hle_audio::editor;

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "invalid params, expected format: <cmd> json_filename out_filename\n");
        return 1;
    }
    const char* json_filename = argv[1];
    const char* out_filename = argv[2];

    data_state_t state = {};
    init(state.node_ids);

    if (!load_store_json(&state, json_filename)) {
        fprintf(stderr, "Couldn't load data from json!\n");
        return 1;
    }
    auto fb_buf = save_store_blob_buffer(&state);

    auto out_f = fopen(out_filename, "wb");
    if (out_f) {
        fwrite(fb_buf.data(), 1, fb_buf.size(), out_f);
        fclose(out_f);
    }

    return 0;
}