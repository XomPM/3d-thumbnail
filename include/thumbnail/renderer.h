#pragma once

#include "thumbnail/mesh.h"
#include "thumbnail/appearance.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace thumbnail {

struct Image {
    unsigned width{};
    unsigned height{};
    std::vector<std::uint8_t> rgba;
};

// Uses offscreen Direct3D 11 WARP; does not open a window or require a GPU.
Image render_thumbnail(const Mesh& mesh, unsigned size, const Appearance& appearance = {});

// Creates a new file only. Existing output files are never overwritten.
void write_png(const Image& image, const std::filesystem::path& path);

} // namespace thumbnail
