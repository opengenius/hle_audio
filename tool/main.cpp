#include "data_types.h"

using namespace hle_audio::editor;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "invalid params, expected format: <cmd> json_filename");
        return 1;
    }
    const char* json_filename = argv[1];

    data_state_t state = {};
    init(state.node_ids);

    load_store_json(&state, json_filename);
    auto fb_buf = save_store_fb_buffer(&state);

    auto out_f = fopen("out_binary", "wb");
    if (out_f) {
        fwrite(fb_buf.data(), 1, fb_buf.size(), out_f);
        fclose(out_f);
    }

    return 0;
}