#pragma once

#include "miniaudio_public.h"
#include "rt_types.h"
#include "alloc_utils.inl"

namespace hle_audio {
namespace rt {

static ma_result read_file(ma_vfs* pVFS, const char* pFilePath, 
        const allocator_t& alloc,
        data_buffer_t* out_read_buffer) {
    ma_result result;
    ma_vfs_file file;
    

    result = ma_vfs_open(pVFS, pFilePath, MA_OPEN_MODE_READ, &file);
    if (result != MA_SUCCESS) {
        return result;
    }

    ma_file_info info;
    result = ma_vfs_info(pVFS, file, &info);
    if (result == MA_SUCCESS) {
        if (info.sizeInBytes <= MA_SIZE_MAX) {
            data_buffer_t res = {};
            res.data = (uint8_t*)allocate(alloc, info.sizeInBytes);
            if (res.data) {
                result = ma_vfs_read(pVFS, file, res.data, (size_t)info.sizeInBytes, &res.size);  /* Safe cast. */
                if (result == MA_SUCCESS) {
                    assert(out_read_buffer != NULL);
                    *out_read_buffer = res;
                } else {
                    deallocate(alloc, res.data);
                }
            }
        } else {
            result = MA_TOO_BIG;
        }
    }
    ma_vfs_close(pVFS, file);

    return result;
}

}
}
