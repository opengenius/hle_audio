#pragma once

namespace hle_audio {
namespace editor {

struct app_state_t;

app_state_t* create_app_state(float scale);
void destroy(app_state_t* state);

void init_with_data(app_state_t* state, const char* filepath, const char* wav_folder);
void process_frame(app_state_t* state);

}
}
