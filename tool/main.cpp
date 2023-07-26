#include "data_types.h"
#include "file_data_provider.h"

using namespace hle_audio::editor;
using namespace hle_audio::data;

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "invalid params, expected format: <cmd> json_filename out_filename sounds_path\n");
        return 1;
    }
    const char* json_filename = argv[1];
    const char* out_filename = argv[2];
    const char* sounds_path = argv[3];

    data_state_t state = {};
    init(state.node_ids);

    if (!load_store_json(&state, json_filename)) {
        fprintf(stderr, "Couldn't load data from json!\n");
        return 1;
    }

    file_data_provider_t fd_prov = {};
    fd_prov._sounds_path = sounds_path;
    fd_prov.use_oggs = true;
    auto fb_buf = save_store_blob_buffer(&state, &fd_prov);

    auto out_f = fopen(out_filename, "wb");
    if (out_f) {
        fwrite(fb_buf.data(), 1, fb_buf.size(), out_f);
        fclose(out_f);
    }

    return 0;
}