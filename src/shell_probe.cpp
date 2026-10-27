#include "thumbnail/process_runner.h"
#include "thumbnail/shell_contract.h"
#include "thumbnail/shell_cache.h"
#include "thumbnail/renderer.h"
#include "windows_helpers.h"
#include <propsys.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <iostream>

namespace {
using Microsoft::WRL::ComPtr;
using namespace thumbnail;
HBITMAP extract(const std::filesystem::path& input, bool shell) {
    HBITMAP bitmap{};
    if (shell) {
        ComPtr<IShellItemImageFactory> factory;
        check_hresult(SHCreateItemFromParsingName(input.c_str(), nullptr, IID_PPV_ARGS(&factory)), "Create Shell item");
        check_hresult(factory->GetImage({512, 512}, SIIGBF_THUMBNAILONLY, &bitmap), "Windows Shell GetImage");
        return bitmap;
    }
    auto suffix = input.extension().wstring();
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), std::towlower);
    if (suffix == L".stp") { suffix = L".step"; }
    if (suffix == L".igs") { suffix = L".iges"; }
    const ShellFormat* format = nullptr;
    for (const auto& candidate : kShellFormats) { if (suffix == candidate.extension) { format = &candidate; } }
    if (!format) { throw std::runtime_error("Unsupported input extension."); }
    const auto dll = executable_path().parent_path() / L"thumbnail-shell.dll";
    const auto module = LoadLibraryW(dll.c_str());
    if (!module) { throw std::runtime_error("Cannot load thumbnail-shell.dll."); }
    // Keep the module loaded until process exit, after all COM references are released.
    const auto get_class = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
    if (!get_class) { throw std::runtime_error("Missing DLL class factory."); }
    ComPtr<IClassFactory> factory;
    check_hresult(get_class(format->clsid, IID_PPV_ARGS(&factory)), "Load class factory");
    ComPtr<IThumbnailProvider> provider;
    check_hresult(factory->CreateInstance(nullptr, IID_PPV_ARGS(&provider)), "Create thumbnail provider");
    if (format->requires_file) {
        ComPtr<IInitializeWithFile> initialize;
        check_hresult(provider.As(&initialize), "Get file initializer");
        check_hresult(initialize->Initialize(input.c_str(), STGM_READ), "Initialize local glTF");
    } else {
        ComPtr<IInitializeWithStream> initialize;
        check_hresult(provider.As(&initialize), "Get stream initializer");
        ComPtr<IStream> stream;
        check_hresult(SHCreateStreamOnFileEx(input.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream), "Open input stream");
        check_hresult(initialize->Initialize(stream.Get(), STGM_READ), "Initialize thumbnail stream");
    }
    WTS_ALPHATYPE alpha{};
    check_hresult(provider->GetThumbnail(512, &bitmap, &alpha), "Extract direct thumbnail");
    return bitmap;
}
void save_bitmap(HBITMAP bitmap, const std::filesystem::path& output) {
    BITMAP description{};
    if (!GetObjectW(bitmap, sizeof(description), &description) || description.bmWidth <= 0 || description.bmHeight <= 0 ||
        description.bmWidth > 1024 || description.bmHeight > 1024) { throw std::runtime_error("Invalid Shell image size."); }
    Image image{static_cast<unsigned>(description.bmWidth), static_cast<unsigned>(description.bmHeight), {}};
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    BITMAPINFO information{};
    information.bmiHeader = {sizeof(BITMAPINFOHEADER), description.bmWidth, -description.bmHeight, 1, 32, BI_RGB};
    const auto dc = GetDC(nullptr);
    const auto copied = GetDIBits(dc, bitmap, 0, image.height, image.rgba.data(), &information, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (copied != static_cast<int>(image.height)) { throw std::runtime_error("Cannot read Shell bitmap."); }
    for (std::size_t offset = 0; offset < image.rgba.size(); offset += 4) {
        std::swap(image.rgba[offset], image.rgba[offset + 2]);
        image.rgba[offset + 3] = 255;
    }
    write_png(image, output);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && (std::wstring(argv[1]) == L"--notify" || std::wstring(argv[1]) == L"--refresh")) {
            const ComApartment apartment;
            const auto input = std::filesystem::absolute(argv[2]);
            if (std::wstring(argv[1]) == L"--refresh") {
                refresh_thumbnails(input);
                std::cout << "Windows thumbnail cache refreshed for this item.\n";
            } else {
                notify_changed(input);
                std::cout << "Shell item update notification delivered.\n";
            }
            return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--progid") {
            const ComApartment apartment;
            wchar_t text[1024]{};
            DWORD length = 1024;
            const auto status = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, argv[2], nullptr, text, &length);
            check_hresult(status, "Query effective ProgID");
            std::wcout << text << '\n';
            return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--diagnose") {
            const ComApartment apartment;
            wchar_t text[1024]{};
            DWORD length = 1024;
            const auto association = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_SHELLEXTENSION, argv[2],
                L"{E357FCCD-A995-4576-B01F-234630154E96}", text, &length);
            std::wcout << L"Association HRESULT=0x" << std::hex << static_cast<unsigned long>(association) << L" CLSID=" << text << L"\n";
            if (FAILED(association)) { return 1; }
            CLSID clsid{};
            check_hresult(CLSIDFromString(text, &clsid), "Parse associated CLSID");
            ComPtr<IThumbnailProvider> provider;
            const auto activation = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&provider));
            std::cout << "Activation HRESULT=0x" << std::hex << static_cast<unsigned long>(activation) << '\n';
            provider.Reset();
            const auto surrogate = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&provider));
            std::cout << "Surrogate HRESULT=0x" << std::hex << static_cast<unsigned long>(surrogate) << '\n';
            return FAILED(activation) ? 1 : 0;
        }
        if (argc != 4 || (std::wstring(argv[1]) != L"--direct" && std::wstring(argv[1]) != L"--shell")) {
            std::cerr << "Usage: shell-probe --direct|--shell INPUT OUTPUT.png\n"
                         "       shell-probe --notify|--refresh INPUT\n"
                         "       shell-probe --progid|--diagnose EXTENSION\n";
            return 2;
        }
        const ComApartment apartment;
        const auto bitmap = extract(std::filesystem::absolute(argv[2]), std::wstring(argv[1]) == L"--shell");
        try { save_bitmap(bitmap, argv[3]); } catch (...) { DeleteObject(bitmap); throw; }
        DeleteObject(bitmap);
        std::cout << "Shell thumbnail extraction succeeded.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
