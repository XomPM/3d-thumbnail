#pragma once
#include <windows.h>
#include <objidl.h>
#include <thumbcache.h>
#include <filesystem>
#include "thumbnail/process_runner.h"

namespace thumbnail {
// temp_root must be writable at the caller's integrity level. No model parser is linked here.
HRESULT thumbnail_from_stream(IStream* stream, const wchar_t* extension, UINT size,
    const std::filesystem::path& worker, const std::filesystem::path& temp_root,
    HBITMAP* bitmap, WTS_ALPHATYPE* alpha, const ProcessLimits& limits = {}) noexcept;
HRESULT thumbnail_from_gltf_file(const std::filesystem::path& input, UINT size,
    const std::filesystem::path& worker, const std::filesystem::path& temp_root,
    HBITMAP* bitmap, WTS_ALPHATYPE* alpha, const ProcessLimits& limits = {}) noexcept;
}
