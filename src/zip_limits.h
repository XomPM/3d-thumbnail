#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <string>

namespace thumbnail {

// Preflight the ZIP directory before lib3mf decompresses XML and attachments.
// ZIP64/multi-volume/encrypted packages are deliberately outside this prototype's limits.
inline void validate_3mf_zip(std::span<const std::byte> bytes) {
    const auto number = [bytes](std::size_t offset, unsigned width) -> std::uint32_t {
        if (offset > bytes.size() || width > bytes.size() - offset) { throw std::runtime_error("Truncated 3MF ZIP."); }
        std::uint32_t result = 0;
        for (unsigned index = 0; index < width; ++index) {
            result |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (8 * index);
        }
        return result;
    };
    if (bytes.size() < 22) { throw std::runtime_error("3MF is not a valid ZIP package."); }
    std::size_t end = bytes.size() - 22;
    const std::size_t search_start = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    while (number(end, 4) != 0x06054b50 || end + 22 + number(end + 20, 2) != bytes.size()) {
        if (end == search_start) { throw std::runtime_error("3MF ZIP directory not found."); }
        --end;
    }
    const auto count = number(end + 10, 2);
    const auto directory_size = number(end + 12, 4);
    std::size_t position = number(end + 16, 4);
    if (number(end + 4, 2) || number(end + 6, 2) || count != number(end + 8, 2) ||
        count == 0 || count > 4096 || position > end || directory_size != end - position) {
        throw std::runtime_error("Unsupported 3MF ZIP directory or entry count.");
    }
    std::uint64_t expanded_total = 0;
    std::set<std::string> names;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (position > end || end - position < 46 || number(position, 4) != 0x02014b50) {
            throw std::runtime_error("Invalid 3MF ZIP entry.");
        }
        const auto expanded = number(position + 24, 4);
        expanded_total += expanded;
        if (expanded > 64 * 1024 * 1024 || expanded_total > 128 * 1024 * 1024 ||
            (number(position + 8, 2) & 1) || number(position + 34, 2) != 0 ||
            (number(position + 10, 2) != 0 && number(position + 10, 2) != 8)) {
            throw std::runtime_error("3MF ZIP exceeds decompression limits or uses unsupported encoding.");
        }
        const auto name_length = number(position + 28, 2);
        const auto entry_length = 46ULL + name_length + number(position + 30, 2) + number(position + 32, 2);
        if (entry_length > end - position) { throw std::runtime_error("Truncated 3MF ZIP entry."); }
        const std::string name(reinterpret_cast<const char*>(bytes.data() + position + 46), name_length);
        if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
            name.find(':') != std::string::npos || name.find('\0') != std::string::npos ||
            name == ".." || name.starts_with("../") || name.find("/../") != std::string::npos ||
            name.ends_with("/..") || !names.insert(name).second) {
            throw std::runtime_error("Unsafe or duplicate 3MF ZIP part name.");
        }
        const auto local = number(position + 42, 4);
        if (local >= position || number(local, 4) != 0x04034b50) { throw std::runtime_error("Invalid 3MF local ZIP header."); }
        position += static_cast<std::size_t>(entry_length);
    }
    if (position != end) { throw std::runtime_error("Inconsistent 3MF ZIP directory size."); }
}

} // namespace thumbnail
