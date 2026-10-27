#pragma once

#include "thumbnail/mesh.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace thumbnail {

Mesh read_model(const std::filesystem::path& path);
Mesh parse_step(std::span<const std::byte> bytes);
Mesh parse_obj(std::span<const std::byte> bytes);
Mesh parse_mesh(std::span<const std::byte> bytes, std::string_view format,
    const std::filesystem::path& resource_directory = {});
Mesh read_iges(const std::filesystem::path& path);
Mesh parse_3mf(std::span<const std::byte> bytes);
std::vector<std::byte> read_model_bytes(const std::filesystem::path& path);
void append_checked_triangle(Mesh& mesh, const Triangle& triangle);

} // namespace thumbnail
