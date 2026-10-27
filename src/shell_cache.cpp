#include "thumbnail/shell_cache.h"
#include "windows_helpers.h"
#include "thumbnail/process_runner.h"
#include <shlobj.h>
#include <thumbcache.h>
#include <wrl/client.h>
#include <array>

namespace thumbnail {
namespace {
class RefreshCheck {
public:
    RefreshCheck() {
        wchar_t temporary[32768]{};
        const auto length = GetTempPathW(32768, temporary);
        if (!length || length >= 32768) { throw std::runtime_error("Cannot locate temporary folder for refresh check."); }
        GUID id{};
        check_hresult(CoCreateGuid(&id), "Create refresh check ID");
        wchar_t name[40]{};
        if (!StringFromGUID2(id, name, 40)) { throw std::runtime_error("Cannot name refresh check."); }
        directory_ = std::filesystem::path(temporary) / (std::wstring(L"3DThumbnailCheck-") + name);
        output_ = directory_ / L"preview.png";
        if (!CreateDirectoryW(directory_.c_str(), nullptr)) { throw std::runtime_error("Cannot create refresh check folder."); }
    }
    ~RefreshCheck() { DeleteFileW(output_.c_str()); RemoveDirectoryW(directory_.c_str()); }
    const std::filesystem::path& output() const { return output_; }
    RefreshCheck(const RefreshCheck&) = delete;
    RefreshCheck& operator=(const RefreshCheck&) = delete;
private:
    std::filesystem::path directory_, output_;
};

void verify_generation(const std::filesystem::path& input) {
    const RefreshCheck check;
    const std::array<std::wstring, 4> arguments{std::filesystem::absolute(input).wstring(), check.output().wstring(), L"--size", L"256"};
    const auto result = run_limited_process(executable_path().parent_path() / L"thumbnail-worker.exe", arguments);
    if (result.timed_out) { throw std::runtime_error("Preview generation exceeded 15 seconds. Existing cache was not refreshed."); }
    if (result.exit_code || !std::filesystem::exists(check.output())) {
        throw std::runtime_error("Preview generation failed; existing cache was not refreshed.\n" + result.diagnostic);
    }
}
}
void notify_changed(const std::filesystem::path& input) {
    PIDLIST_ABSOLUTE item{};
    check_hresult(SHParseDisplayName(input.c_str(), nullptr, &item, 0, nullptr), "Parse changed Shell item");
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_IDLIST | SHCNF_FLUSH, item, nullptr);
    CoTaskMemFree(item);
}
void refresh_thumbnails(const std::filesystem::path& input) {
    // A failed extraction can replace a previously usable Shell cache entry.
    // Check our worker before asking Windows to discard/re-extract any size.
    verify_generation(input);
    Microsoft::WRL::ComPtr<IShellItem> item;
    check_hresult(SHCreateItemFromParsingName(input.c_str(), nullptr, IID_PPV_ARGS(&item)), "Create Shell item");
    Microsoft::WRL::ComPtr<IThumbnailCache> cache;
    check_hresult(CoCreateInstance(CLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&cache)), "Open Windows thumbnail cache");
    for (const UINT size : {96U, 256U, 512U, 1024U}) {
        check_hresult(cache->GetThumbnail(item.Get(), size, WTS_FORCEEXTRACTION, nullptr, nullptr, nullptr),
            "Refresh cached thumbnail");
    }
    notify_changed(input);
}
}
