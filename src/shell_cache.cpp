#include "thumbnail/shell_cache.h"
#include "windows_helpers.h"
#include <shlobj.h>
#include <thumbcache.h>
#include <wrl/client.h>

namespace thumbnail {
void notify_changed(const std::filesystem::path& input) {
    PIDLIST_ABSOLUTE item{};
    check_hresult(SHParseDisplayName(input.c_str(), nullptr, &item, 0, nullptr), "Parse changed Shell item");
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_IDLIST | SHCNF_FLUSH, item, nullptr);
    CoTaskMemFree(item);
}
void refresh_thumbnails(const std::filesystem::path& input) {
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
