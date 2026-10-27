#include "thumbnail/renderer.h"
#include "windows_helpers.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <memory>
#include <stdexcept>

namespace thumbnail {
namespace {

using Microsoft::WRL::ComPtr;

std::vector<std::uint8_t> encode_png(const Image& image) {
    ComPtr<IWICImagingFactory> factory;
    check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)), "Create PNG encoder factory");
    ComPtr<IStream> stream;
    check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create PNG memory stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PNG encoder");
    check_hresult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    check_hresult(encoder->CreateNewFrame(&frame, nullptr), "Create PNG frame");
    check_hresult(frame->Initialize(nullptr), "Initialize PNG frame");
    check_hresult(frame->SetSize(image.width, image.height), "Set PNG dimensions");
    auto format = GUID_WICPixelFormat32bppRGBA;
    check_hresult(frame->SetPixelFormat(&format), "Set PNG pixel format");
    ComPtr<IWICBitmap> bitmap;
    check_hresult(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppRGBA, image.width * 4,
        static_cast<UINT>(image.rgba.size()), const_cast<BYTE*>(image.rgba.data()), &bitmap), "Create PNG source bitmap");
    // WIC can negotiate another channel order (for example BGRA). Convert the source explicitly.
    ComPtr<IWICFormatConverter> converter;
    check_hresult(factory->CreateFormatConverter(&converter), "Create PNG pixel converter");
    check_hresult(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0,
        WICBitmapPaletteTypeCustom), "Convert PNG pixels");
    check_hresult(frame->WriteSource(converter.Get(), nullptr), "Encode PNG pixels");
    check_hresult(frame->Commit(), "Commit PNG frame");
    check_hresult(encoder->Commit(), "Commit PNG image");
    STATSTG stats{};
    check_hresult(stream->Stat(&stats, STATFLAG_NONAME), "Read PNG stream size");
    constexpr ULONGLONG maximum_encoded_bytes = 32 * 1024 * 1024;
    if (stats.cbSize.QuadPart > maximum_encoded_bytes) {
        throw std::runtime_error("Encoded PNG exceeds the output limit.");
    }
    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(stats.cbSize.QuadPart));
    check_hresult(stream->Seek({}, STREAM_SEEK_SET, nullptr), "Rewind PNG stream");
    ULONG bytes_read = 0;
    check_hresult(stream->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &bytes_read), "Read encoded PNG");
    if (bytes_read != encoded.size()) {
        throw std::runtime_error("Incomplete PNG encoding.");
    }
    return encoded;
}

struct CloseFile {
    void operator()(void* handle) const { CloseHandle(handle); }
};

} // namespace

void write_png(const Image& image, const std::filesystem::path& path) {
    if (image.width == 0 || image.height == 0 || image.width > 1024 || image.height > 1024 ||
        image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
        throw std::runtime_error("Invalid RGBA image dimensions or buffer size.");
    }
    const ComApartment apartment;
    const auto encoded = encode_png(image);
    const auto raw_handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw_handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot create output. Choose a new filename in a writable folder.");
    }
    std::unique_ptr<void, CloseFile> handle(raw_handle);
    DWORD bytes_written = 0;
    if (!WriteFile(handle.get(), encoded.data(), static_cast<DWORD>(encoded.size()), &bytes_written, nullptr) ||
        bytes_written != encoded.size()) {
        handle.reset();
        std::error_code cleanup_error;
        std::filesystem::remove(path, cleanup_error); // Only the new file created above belongs to this operation.
        throw std::runtime_error("Cannot write the complete output PNG.");
    }
}

} // namespace thumbnail
