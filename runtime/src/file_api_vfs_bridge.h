#pragma once

#include "miniaudio.h"

struct hlea_file_ti;

struct vfs_bridge_t {
    ma_vfs_callbacks cb;
    const hlea_file_ti* file_api_vt;
    void* sys;
};

void init(vfs_bridge_t& impl, const hlea_file_ti* file_api_vt, void* sys);