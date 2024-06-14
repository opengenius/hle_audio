#pragma once

namespace ImGuiExt {

bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f);
bool InputText(const char* label, const char* hint, std::string* str, ImGuiInputTextFlags flags = 0);

}
