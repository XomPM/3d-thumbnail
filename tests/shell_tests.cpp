#include "stream_thumbnail.h"
#include "thumbnail/shell_contract.h"
#include "windows_helpers.h"
#include <propsys.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using namespace thumbnail;
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
ComPtr<IStream> memory_stream(const std::string& content) {
    ComPtr<IStream> result;
    result.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(content.data()), static_cast<UINT>(content.size())));
    require(result.Get() != nullptr, "Cannot create test stream.");
    return result;
}
ComPtr<IStream> file_memory(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Missing sample.");
    return memory_stream(std::string(std::istreambuf_iterator<char>(file), {}));
}
void visible(HBITMAP bitmap, UINT size, WTS_ALPHATYPE alpha) {
    require(bitmap && alpha == WTSAT_RGB, "Wrong bitmap or alpha contract.");
    DIBSECTION dib{};
    require(GetObjectW(bitmap, sizeof(dib), &dib) == sizeof(dib), "Thumbnail is not a DIB section.");
    require(dib.dsBm.bmWidth == static_cast<LONG>(size) && dib.dsBm.bmHeight == static_cast<LONG>(size) && dib.dsBm.bmBitsPixel == 32, "Wrong bitmap dimensions or depth.");
    const auto* bytes = static_cast<BYTE*>(dib.dsBm.bmBits);
    unsigned colored = 0;
    for (UINT index = 0; index < size * size; ++index) { if (bytes[index * 4 + 2] > 80) { ++colored; } }
    require(colored > size * size / 40 && colored < size * size * 9 / 10, "Bitmap is blank or clipped.");
}

void bridge_tests(const std::filesystem::path& samples, const std::filesystem::path& temporary) {
    const auto directory = executable_path().parent_path();
    const auto worker = directory / "thumbnail-worker.exe";
    for (UINT size : {16U, 32U, 256U, 1024U, 2048U}) {
        auto stream = file_memory(samples / "bearing_binary.stl");
        check_hresult(stream->Seek({7}, STREAM_SEEK_SET, nullptr), "Move test stream");
        HBITMAP bitmap{}; WTS_ALPHATYPE alpha{};
        check_hresult(thumbnail_from_stream(stream.Get(), L".stl", size, worker, temporary, &bitmap, &alpha), "Stream thumbnail");
        visible(bitmap, std::min(size, 1024U), alpha);
        DeleteObject(bitmap);
        ULARGE_INTEGER position{};
        check_hresult(stream->Seek({}, STREAM_SEEK_CUR, &position), "Check stream cursor");
        require(position.QuadPart == 7, "Input stream position changed.");
        require(std::filesystem::is_empty(temporary), "Successful thumbnail leaked temporary files.");
    }
    auto stream = memory_stream("invalid model");
    HBITMAP bitmap = reinterpret_cast<HBITMAP>(1); WTS_ALPHATYPE alpha = WTSAT_ARGB;
    require(FAILED(thumbnail_from_stream(stream.Get(), L".stl", 256, worker, temporary, &bitmap, &alpha)), "Bad model accepted.");
    require(!bitmap && alpha == WTSAT_UNKNOWN, "Failure published a bitmap.");
    require(std::filesystem::is_empty(temporary), "Bad model leaked temporary files.");
    const auto helper = directory / "process-fixture.exe";
    for (const auto* action : {"partial", "failure", "missing"}) {
        stream = memory_stream(action);
        require(FAILED(thumbnail_from_stream(stream.Get(), L".stl", 256, helper, temporary, &bitmap, &alpha)), "Bad worker output accepted.");
        require(!bitmap && std::filesystem::is_empty(temporary), "Failed worker leaked output.");
    }
    stream = memory_stream("sleep");
    ProcessLimits short_limit;
    short_limit.timeout = std::chrono::milliseconds(100);
    require(thumbnail_from_stream(stream.Get(), L".stl", 256, helper, temporary, &bitmap, &alpha, short_limit) == HRESULT_FROM_WIN32(ERROR_TIMEOUT), "Timeout HRESULT lost.");
    require(!bitmap && std::filesystem::is_empty(temporary), "Timeout leaked temporary files.");
    require(thumbnail_from_stream(stream.Get(), L".stl", 0, worker, temporary, &bitmap, &alpha) == E_INVALIDARG, "Zero size accepted.");
    require(thumbnail_from_stream(stream.Get(), L".stl", 256, worker, temporary, nullptr, &alpha) == E_POINTER, "Null output accepted.");
    std::cout << "PASS stream cursor, bitmap sizes, malformed/partial output, timeout, temporary cleanup\n";
}

void com_tests(const std::filesystem::path& samples) {
    const auto path = executable_path().parent_path() / "thumbnail-shell.dll";
    const auto module = LoadLibraryW(path.c_str());
    require(module != nullptr, "Cannot load shell DLL.");
    const auto get_class = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
    const auto unload = reinterpret_cast<HRESULT(__stdcall*)()>(GetProcAddress(module, "DllCanUnloadNow"));
    require(get_class && unload && unload() == S_OK, "Missing COM exports.");
    const char* files[]{"bearing_binary.stl", "bearing.obj", "bearing.3mf", "bearing_ap214.step",
        "bearing.ply", "bearing.glb", "bearing.gltf", "bearing.iges", "bearing.fbx"};
    for (unsigned index = 0; index < kShellFormats.size(); ++index) {
        ComPtr<IClassFactory> factory;
        check_hresult(get_class(kShellFormats[index].clsid, IID_PPV_ARGS(&factory)), "Get factory");
        require(unload() == S_FALSE, "DLL unloads with live factory.");
        check_hresult(factory->LockServer(TRUE), "Lock server");
        check_hresult(factory->LockServer(FALSE), "Unlock server");
        ComPtr<IThumbnailProvider> provider;
        check_hresult(factory->CreateInstance(nullptr, IID_PPV_ARGS(&provider)), "Create provider");
        HBITMAP bitmap{}; WTS_ALPHATYPE alpha{};
        require(provider->GetThumbnail(128, &bitmap, &alpha) == E_UNEXPECTED, "Uninitialized provider accepted.");
        if (kShellFormats[index].requires_file) {
            ComPtr<IInitializeWithFile> initialize;
            check_hresult(provider.As(&initialize), "Query file initialization");
            const auto file = std::filesystem::absolute(samples / files[index]);
            require(initialize->Initialize(L"relative.gltf", STGM_READ) == E_INVALIDARG, "Relative path accepted.");
            check_hresult(initialize->Initialize(file.c_str(), STGM_READ), "Initialize local glTF");
            require(initialize->Initialize(file.c_str(), STGM_READ) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED), "Repeated file initialization accepted.");
        } else {
            ComPtr<IInitializeWithStream> initialize;
            check_hresult(provider.As(&initialize), "Query stream initialization");
            auto stream = file_memory(samples / files[index]); // Intentionally has no filename.
            check_hresult(initialize->Initialize(stream.Get(), STGM_READ), "Initialize provider");
            require(initialize->Initialize(stream.Get(), STGM_READ) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED), "Repeated initialization accepted.");
        }
        check_hresult(provider->GetThumbnail(128, &bitmap, &alpha), "COM GetThumbnail");
        visible(bitmap, 128, alpha);
        DeleteObject(bitmap);
        std::cout << "PASS COM provider: " << files[index] << '\n';
    }
    require(unload() == S_OK, "COM objects leaked.");
    ComPtr<IClassFactory> unknown;
    require(get_class(CLSID_NULL, IID_PPV_ARGS(&unknown)) == CLASS_E_CLASSNOTAVAILABLE, "Unknown class accepted.");
    FreeLibrary(module);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 3, "Usage: shell-tests <samples> <test-output>");
        const ComApartment apartment;
        const auto temporary = std::filesystem::absolute(std::filesystem::path(argv[2]) /
            (L"셸 스트림 검증 " + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count())));
        std::filesystem::create_directories(temporary);
        bridge_tests(argv[1], temporary);
        std::filesystem::remove(temporary);
        com_tests(argv[1]);
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
