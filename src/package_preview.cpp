#include "thumbnail/package_preview.h"
#include "thumbnail/mesh.h"
#include "windows_helpers.h"
#include "zip_limits.h"
#include <msopc.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>

namespace thumbnail {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t kThumbnailRelation[] = L"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail";
constexpr wchar_t kModelRelation[] = L"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel";

ComPtr<IStream> memory_stream(const void* data, ULONG size) {
    ComPtr<IStream> stream;
    check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create preview stream");
    ULONG written{};
    check_hresult(stream->Write(data, size, &written), "Write preview stream");
    if (written != size) { throw std::runtime_error("Incomplete preview stream."); }
    check_hresult(stream->Seek({}, STREAM_SEEK_SET, nullptr), "Rewind preview stream");
    return stream;
}

ComPtr<IOpcPart> related_part(IOpcPackage* package, const wchar_t* type) {
    ComPtr<IOpcRelationshipSet> relationships;
    check_hresult(package->GetRelationshipSet(&relationships), "Read package relationships");
    ComPtr<IOpcRelationshipEnumerator> entries;
    check_hresult(relationships->GetEnumeratorForType(type, &entries), "Find package relationship");
    BOOL found{};
    check_hresult(entries->MoveNext(&found), "Read package relationship");
    if (!found) { throw std::runtime_error("3MF has no standard package preview or model relationship."); }
    ComPtr<IOpcRelationship> relationship;
    check_hresult(entries->GetCurrent(&relationship), "Read preview relationship");
    check_hresult(entries->MoveNext(&found), "Check duplicate relationships");
    if (found) { throw std::runtime_error("Ambiguous 3MF package relationships."); }
    OPC_URI_TARGET_MODE mode{};
    check_hresult(relationship->GetTargetMode(&mode), "Check relationship mode");
    if (mode != OPC_URI_TARGET_MODE_INTERNAL) { throw std::runtime_error("External 3MF preview/model relationships are not allowed."); }
    ComPtr<IUri> target;
    ComPtr<IOpcUri> source;
    ComPtr<IOpcPartUri> name;
    check_hresult(relationship->GetTargetUri(&target), "Read relationship target");
    check_hresult(relationship->GetSourceUri(&source), "Read relationship source");
    // Resolve only to an OPC part, never to a filesystem path or network resource.
    check_hresult(source->CombinePartUri(target.Get(), &name), "Resolve package part");
    ComPtr<IOpcPartSet> parts;
    check_hresult(package->GetPartSet(&parts), "Read package parts");
    ComPtr<IOpcPart> part;
    check_hresult(parts->GetPart(name.Get(), &part), "Read related package part");
    return part;
}

Image decode_preview(IStream* stream, unsigned size) {
    ComPtr<IWICImagingFactory> factory;
    check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)), "Create preview decoder");
    ComPtr<IWICBitmapDecoder> decoder;
    check_hresult(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder), "Decode stored preview");
    GUID format{};
    check_hresult(decoder->GetContainerFormat(&format), "Check preview format");
    if (format != GUID_ContainerFormatPng && format != GUID_ContainerFormatJpeg) {
        throw std::runtime_error("Stored 3MF preview must be PNG or JPEG.");
    }
    UINT frames{};
    check_hresult(decoder->GetFrameCount(&frames), "Check preview frames");
    if (frames != 1) { throw std::runtime_error("Stored preview must have one frame."); }
    ComPtr<IWICBitmapFrameDecode> frame;
    check_hresult(decoder->GetFrame(0, &frame), "Read preview frame");
    UINT width{}, height{};
    check_hresult(frame->GetSize(&width, &height), "Read preview dimensions");
    if (!width || !height || width > 4096 || height > 4096) {
        throw std::runtime_error("Stored preview exceeds the 4096 pixel dimension limit.");
    }
    ComPtr<IWICFormatConverter> converter;
    check_hresult(factory->CreateFormatConverter(&converter), "Create preview converter");
    check_hresult(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom), "Convert stored preview");
    // Convert to WIC's canonical premultiplied BGRA and reorder explicitly below; converting
    // 24-bit RGB previews straight to PRGBA swapped red and blue on real Bambu Studio files.
    // Decode the complete source before accepting it, including pixels outside the scaled sample.
    std::vector<BYTE> decoded(static_cast<std::size_t>(width) * height * 4);
    check_hresult(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(decoded.size()), decoded.data()), "Validate preview pixels");
    ComPtr<IWICBitmap> bitmap;
    check_hresult(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA,
        width * 4, static_cast<UINT>(decoded.size()), decoded.data(), &bitmap), "Create stored preview bitmap");
    const auto longest = std::max(width, height);
    const UINT scaled_width = std::max(1U, width * size / longest);
    const UINT scaled_height = std::max(1U, height * size / longest);
    ComPtr<IWICBitmapScaler> scaler;
    check_hresult(factory->CreateBitmapScaler(&scaler), "Create preview scaler");
    check_hresult(scaler->Initialize(bitmap.Get(), scaled_width, scaled_height, WICBitmapInterpolationModeFant), "Scale stored preview");
    std::vector<BYTE> pixels(static_cast<std::size_t>(scaled_width) * scaled_height * 4);
    check_hresult(scaler->CopyPixels(nullptr, scaled_width * 4, static_cast<UINT>(pixels.size()), pixels.data()), "Read scaled preview");
    Image result{size, size, std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size * 4)};
    for (UINT y = 0; y < scaled_height; ++y) {
        for (UINT x = 0; x < scaled_width; ++x) {
            const auto from = (static_cast<std::size_t>(y) * scaled_width + x) * 4;
            const auto to = ((y + (size - scaled_height) / 2) * static_cast<std::size_t>(size) + x + (size - scaled_width) / 2) * 4;
            const unsigned alpha = pixels[from + 3];
            for (unsigned channel = 0; channel < 3; ++channel) {
                const unsigned premultiplied = pixels[from + 2 - channel]; // BGRA source, RGBA output.
                result.rgba[to + channel] = static_cast<std::uint8_t>(alpha ? std::min(255U, (premultiplied * 255U + alpha / 2) / alpha) : 0);
            }
            result.rgba[to + 3] = static_cast<std::uint8_t>(alpha);
        }
    }
    return result;
}
}

Image read_3mf_preview(std::span<const std::byte> bytes, unsigned size) {
    if (bytes.empty() || bytes.size() > kMaximumFileBytes || size < 64 || size > 1024) {
        throw std::runtime_error("Invalid 3MF preview input size.");
    }
    validate_3mf_zip(bytes);
    const ComApartment apartment;
    const auto stream = memory_stream(bytes.data(), static_cast<ULONG>(bytes.size()));
    ComPtr<IOpcFactory> factory;
    check_hresult(CoCreateInstance(__uuidof(OpcFactory), nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)), "Create package reader");
    ComPtr<IOpcPackage> package;
    check_hresult(factory->ReadPackageFromStream(stream.Get(), OPC_VALIDATE_ON_LOAD, &package), "Read 3MF package preview");
    related_part(package.Get(), kModelRelation); // A preview alone is not a 3MF package.
    const auto part = related_part(package.Get(), kThumbnailRelation);
    ComPtr<IStream> content;
    check_hresult(part->GetContentStream(&content), "Read stored preview bytes");
    STATSTG stats{};
    check_hresult(content->Stat(&stats, STATFLAG_NONAME), "Check stored preview size");
    if (!stats.cbSize.QuadPart || stats.cbSize.QuadPart > 4 * 1024 * 1024) {
        throw std::runtime_error("Stored preview exceeds the 4 MiB limit or is empty.");
    }
    std::vector<BYTE> encoded(static_cast<std::size_t>(stats.cbSize.QuadPart));
    ULONG received{};
    check_hresult(content->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &received), "Read complete preview");
    if (received != encoded.size()) { throw std::runtime_error("Truncated stored preview."); }
    return decode_preview(memory_stream(encoded.data(), received).Get(), size);
}
}
