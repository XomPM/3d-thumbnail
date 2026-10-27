#pragma once
#include <assimp/IOSystem.hpp>
#include <filesystem>
#include <memory>
#include <span>

namespace thumbnail {
// Only declared glTF buffers are loaded, within the model directory and the byte budget.
std::unique_ptr<Assimp::IOSystem> gltf_resources(std::span<const std::byte> json,
    const std::filesystem::path& directory);
}
