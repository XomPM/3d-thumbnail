#pragma once

#include "thumbnail/mesh.h"

#include <filesystem>
#include <span>

namespace thumbnail {

// The byte-based entry point can later consume an isolated Shell IStream.
Mesh parse_stl(std::span<const std::byte> bytes);
Mesh read_stl(const std::filesystem::path& path);

} // namespace thumbnail
