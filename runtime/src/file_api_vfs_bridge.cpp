#include "file_api_vfs_bridge.h"
#include "hlea/file_types.h"
#include <cassert>

/**
 * vfs implementation
 */
static ma_result vfs_bridge_onOpen(ma_vfs* pVFS, const char* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile) {
    auto vfs = (vfs_bridge_t*)pVFS;

    auto file_h = vfs->file_api_vt->open(vfs->sys, pFilePath);
    
    if (!file_h) return MA_DOES_NOT_EXIST;

    *pFile = (ma_vfs_file)file_h;
    return MA_SUCCESS;
}

static ma_result vfs_bridge_onOpenW(ma_vfs* pVFS, const wchar_t* pFilePath, ma_uint32 openMode, ma_vfs_file* pFile) {
    assert(false);
    return MA_ERROR;
}

static ma_result vfs_bridge_onClose(ma_vfs* pVFS, ma_vfs_file file) {
    auto vfs = (vfs_bridge_t*)pVFS;
    auto file_h = (hlea_file_handle_t)(intptr_t)file;

    vfs->file_api_vt->close(vfs->sys, file_h);

    return MA_SUCCESS;
}

static ma_result vfs_bridge_onRead(ma_vfs* pVFS, ma_vfs_file file, void* pDst, size_t sizeInBytes, size_t* pBytesRead) {
    auto vfs = (vfs_bridge_t*)pVFS;
    auto file_h = (hlea_file_handle_t)(intptr_t)file;

    *pBytesRead = vfs->file_api_vt->read(vfs->sys, file_h, pDst, sizeInBytes);

    return MA_SUCCESS;
}

static ma_result vfs_bridge_onWrite(ma_vfs* pVFS, ma_vfs_file file, const void* pSrc, size_t sizeInBytes, size_t* pBytesWritten) {
    assert(false);
    return MA_ERROR;
}

static ma_result vfs_bridge_onSeek(ma_vfs* pVFS, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin) {
    auto vfs = (vfs_bridge_t*)pVFS;
    auto file_h = (hlea_file_handle_t)(intptr_t)file;

    size_t absolute_offset = 0u;
    switch (origin)
    {
        case ma_seek_origin_start:
            absolute_offset = (size_t)offset;
        break;
        case ma_seek_origin_current:
            absolute_offset = vfs->file_api_vt->tell(vfs->sys, file_h) + offset;
        break;
        case ma_seek_origin_end:
            absolute_offset = vfs->file_api_vt->size(vfs->sys, file_h) - offset;
        break;
    
    default:
        break;
    }

    vfs->file_api_vt->seek(vfs->sys, file_h, absolute_offset);

    return MA_SUCCESS;
}

static ma_result vfs_bridge_onTell(ma_vfs* pVFS, ma_vfs_file file, ma_int64* pCursor) {
    auto vfs = (vfs_bridge_t*)pVFS;
    auto file_h = (hlea_file_handle_t)(intptr_t)file;

    *pCursor = (ma_int64)vfs->file_api_vt->tell(vfs->sys, file_h);

    return MA_SUCCESS;
}

static ma_result vfs_bridge_onInfo(ma_vfs* pVFS, ma_vfs_file file, ma_file_info* pInfo) {
    auto vfs = (vfs_bridge_t*)pVFS;
    auto file_h = (hlea_file_handle_t)(intptr_t)file;

    pInfo->sizeInBytes = vfs->file_api_vt->size(vfs->sys, file_h);

    return MA_SUCCESS;
}

static const ma_vfs_callbacks file_vt_bridge_vfs_cb = {
    vfs_bridge_onOpen,
    vfs_bridge_onOpenW,
    vfs_bridge_onClose,
    vfs_bridge_onRead,
    vfs_bridge_onWrite,
    vfs_bridge_onSeek,
    vfs_bridge_onTell,
    vfs_bridge_onInfo
};

void init(vfs_bridge_t& impl, const hlea_file_ti* file_api_vt, void* sys) {
    impl.cb = file_vt_bridge_vfs_cb;
    impl.file_api_vt = file_api_vt;
    impl.sys = sys;
}