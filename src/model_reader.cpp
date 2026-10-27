#include "thumbnail/model_reader.h"
#include "thumbnail/stl_reader.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <stdexcept>

namespace thumbnail {

void append_checked_triangle(Mesh& mesh, const Triangle& triangle) {
    if (mesh.triangles.size() >= kMaximumTriangles) {
        throw std::runtime_error("Model exceeds the 250000 triangle limit.");
    }
    for (const auto point : triangle) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)}) > 1e30) {
            throw std::runtime_error("Model contains invalid or excessively large coordinates.");
        }
    }
    mesh.triangles.push_back(triangle);
}

std::vector<std::byte> read_model_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { throw std::runtime_error("Cannot open input model."); }
    const auto length = input.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(kMaximumFileBytes)) {
        throw std::runtime_error("Model must be nonempty and no larger than 64 MiB.");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length))) {
        throw std::runtime_error("Cannot read the complete input model.");
    }
    return bytes;
}

Mesh read_model(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    if (extension != L".stl" && extension != L".step" && extension != L".stp" &&
        extension != L".obj" && extension != L".3mf" && extension != L".ply" && extension != L".glb" &&
        extension != L".gltf" && extension != L".fbx" && extension != L".igs" && extension != L".iges") {
        throw std::runtime_error("Supported formats: STL, STEP/STP, OBJ, 3MF, GLB/glTF, PLY, IGES/IGS, FBX.");
    }
    if (extension == L".igs" || extension == L".iges") { return read_iges(path); }
    const auto bytes = read_model_bytes(path);
    if (extension == L".stl") { return parse_stl(bytes); }
    if (extension == L".step" || extension == L".stp") { return parse_step(bytes); }
    if (extension == L".obj") { return parse_obj(bytes); }
    if (extension == L".ply") { return parse_mesh(bytes, "ply"); }
    if (extension == L".fbx") { return parse_mesh(bytes, "fbx"); }
    if (extension == L".glb") { return parse_mesh(bytes, "glb"); }
    if (extension == L".gltf") { return parse_mesh(bytes, "gltf", std::filesystem::absolute(path).parent_path()); }
    return parse_3mf(bytes);
}

} // namespace thumbnail
