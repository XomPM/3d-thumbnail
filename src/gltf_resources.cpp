#include "gltf_resources.h"
#include "thumbnail/mesh.h"
#include <assimp/IOStream.hpp>
#include <rapidjson/document.h>
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace thumbnail {
namespace {
using Bytes = std::vector<std::byte>;
struct CloseFile { void operator()(void* h) const { if (h && h != INVALID_HANDLE_VALUE) { CloseHandle(h); } } };
using File = std::unique_ptr<void, CloseFile>;

std::wstring resolved_name(HANDLE handle) {
    std::wstring result(32768, L'\0');
    const auto count = GetFinalPathNameByHandleW(handle, result.data(), static_cast<DWORD>(result.size()), 0);
    if (!count || count >= result.size()) { throw std::runtime_error("Cannot resolve glTF resource path."); }
    result.resize(count);
    return result;
}

std::filesystem::path relative_uri(const std::string& uri) {
    std::string decoded;
    auto hex = [](char value) -> int {
        if (value >= '0' && value <= '9') { return value - '0'; }
        if (value >= 'a' && value <= 'f') { return value - 'a' + 10; }
        if (value >= 'A' && value <= 'F') { return value - 'A' + 10; }
        return -1;
    };
    for (std::size_t index = 0; index < uri.size(); ++index) {
        char value = uri[index];
        if (value == '%') {
            if (index + 2 >= uri.size() || hex(uri[index + 1]) < 0 || hex(uri[index + 2]) < 0) {
                throw std::runtime_error("Invalid glTF buffer URI encoding.");
            }
            value = static_cast<char>(hex(uri[index + 1]) * 16 + hex(uri[index + 2]));
            index += 2;
        }
        if (static_cast<unsigned char>(value) < 32 || value == ':' || value == '?' || value == '#' || value == '\\') {
            throw std::runtime_error("Only local relative glTF buffer URIs are supported.");
        }
        decoded.push_back(value);
    }
    if (decoded.empty() || decoded.size() > 4096) { throw std::runtime_error("Invalid glTF buffer path length."); }
    const auto path = std::filesystem::path(std::u8string(decoded.begin(), decoded.end()));
    if (path.has_root_path()) { throw std::runtime_error("Absolute glTF resource path is forbidden."); }
    for (const auto& part : path) {
        const auto name = part.wstring();
        if (name == L".." || (!name.empty() && (name.back() == L' ' || (name.back() == L'.' && name != L".")))) {
            throw std::runtime_error("glTF resource path escapes the model directory.");
        }
    }
    return path;
}

Bytes read_buffer(const std::filesystem::path& directory, const std::filesystem::path& relative,
    const std::wstring& root, std::size_t& remaining) {
    const auto path = directory / relative;
    File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE || GetFileType(file.get()) != FILE_TYPE_DISK) {
        throw std::runtime_error("Cannot open local glTF buffer.");
    }
    const auto actual = resolved_name(file.get());
    if (actual.size() <= root.size() || _wcsnicmp(actual.c_str(), root.c_str(), root.size()) != 0) {
        throw std::runtime_error("glTF buffer resolves outside the model directory.");
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart <= 0 ||
        static_cast<unsigned long long>(length.QuadPart) > remaining) {
        throw std::runtime_error("glTF input and external buffers exceed 64 MiB or contain an empty buffer.");
    }
    Bytes bytes(static_cast<std::size_t>(length.QuadPart));
    DWORD received{};
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) || received != bytes.size()) {
        throw std::runtime_error("Cannot read complete glTF buffer.");
    }
    remaining -= bytes.size();
    return bytes;
}

class BufferStream final : public Assimp::IOStream {
public:
    explicit BufferStream(const Bytes& bytes) : bytes_(bytes) {}
    size_t Read(void* destination, size_t size, size_t count) override {
        if (!size || !destination) { return 0; }
        const auto available = std::min(count, (bytes_.size() - position_) / size);
        std::memcpy(destination, bytes_.data() + position_, available * size);
        position_ += available * size;
        return available;
    }
    size_t Write(const void*, size_t, size_t) override { return 0; }
    aiReturn Seek(size_t offset, aiOrigin origin) override {
        if (origin == aiOrigin_END) {
            if (offset > bytes_.size()) { return aiReturn_FAILURE; }
            position_ = bytes_.size() - offset;
        } else {
            const auto base = origin == aiOrigin_CUR ? position_ : 0;
            if (offset > bytes_.size() - base) { return aiReturn_FAILURE; }
            position_ = base + offset;
        }
        return aiReturn_SUCCESS;
    }
    size_t Tell() const override { return position_; }
    size_t FileSize() const override { return bytes_.size(); }
    void Flush() override {}
private:
    const Bytes& bytes_;
    size_t position_{};
};

class BufferFiles final : public Assimp::IOSystem {
public:
    std::map<std::string, Bytes> buffers;
    bool Exists(const char* path) const override { return path && buffers.contains(path); }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* path, const char* mode) override {
        if (!path || !mode || std::strchr(mode, 'w') || std::strchr(mode, 'a') || std::strchr(mode, '+')) { return nullptr; }
        const auto found = buffers.find(path);
        return found == buffers.end() ? nullptr : new BufferStream(found->second);
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }
};
}

std::unique_ptr<Assimp::IOSystem> gltf_resources(std::span<const std::byte> json,
    const std::filesystem::path& directory) {
    auto files = std::make_unique<BufferFiles>();
    if (json.empty() || json.size() > kMaximumFileBytes) { throw std::runtime_error("Invalid glTF JSON size."); }
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(reinterpret_cast<const char*>(json.data()), json.size());
    if (document.HasParseError() || !document.IsObject()) { throw std::runtime_error("Invalid glTF JSON."); }
    if (!document.HasMember("buffers")) { return files; }
    const auto& buffers = document["buffers"];
    if (!buffers.IsArray() || buffers.Size() > 128) { throw std::runtime_error("Unsupported glTF buffer count."); }
    File root_handle;
    std::wstring root;
    auto remaining = kMaximumFileBytes - json.size();
    std::size_t declared = 0;
    for (const auto& buffer : buffers.GetArray()) {
        if (!buffer.IsObject() || !buffer.HasMember("byteLength") || !buffer["byteLength"].IsUint64()) {
            throw std::runtime_error("Invalid glTF buffer length.");
        }
        const auto length = buffer["byteLength"].GetUint64();
        if (length > kMaximumFileBytes - declared) { throw std::runtime_error("glTF decoded buffers exceed 64 MiB."); }
        declared += static_cast<std::size_t>(length);
        if (!buffer.HasMember("uri")) { continue; }
        if (!buffer["uri"].IsString()) { throw std::runtime_error("Invalid glTF buffer URI."); }
        const std::string uri(buffer["uri"].GetString(), buffer["uri"].GetStringLength());
        if (uri.starts_with("data:")) { continue; }
        const auto relative = relative_uri(uri);
        if (files->buffers.contains(uri)) { continue; }
        if (directory.empty()) { throw std::runtime_error("External glTF buffers require a local model file."); }
        if (!root_handle) {
            root_handle.reset(CreateFileW(directory.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            if (root_handle.get() == INVALID_HANDLE_VALUE) { throw std::runtime_error("Cannot open glTF model directory."); }
            root = resolved_name(root_handle.get());
            if (root.back() != L'\\') { root += L'\\'; }
        }
        files->buffers.emplace(uri, read_buffer(directory, relative, root, remaining));
    }
    return files;
}
}
