#include "thumbnail/model_reader.h"
#include "gltf_resources.h"

#include <assimp/Importer.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stdexcept>

namespace thumbnail {
namespace {

class NoExternalFiles final : public Assimp::IOSystem {
public:
    bool Exists(const char*) const override { return false; }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char*, const char*) override { return nullptr; }
    void Close(Assimp::IOStream*) override {}
};

} // namespace

Mesh parse_obj(std::span<const std::byte> bytes) {
    return parse_mesh(bytes, "obj");
}

Mesh parse_mesh(std::span<const std::byte> bytes, std::string_view format, const std::filesystem::path& resource_directory) {
    if (format != "obj" && format != "ply" && format != "glb" && format != "gltf" && format != "fbx") {
        throw std::runtime_error("Unsupported mesh format.");
    }
    if (bytes.empty() || bytes.size() > kMaximumFileBytes) {
        throw std::runtime_error("Mesh input must be nonempty and no larger than 64 MiB.");
    }
    Assimp::Importer importer;
    if (format == "gltf") { importer.SetIOHandler(gltf_resources(bytes, resource_directory).release()); }
    else { importer.SetIOHandler(new NoExternalFiles()); }
    const std::string hint(format);
    const auto* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(),
        aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_ValidateDataStructure, hint.c_str());
    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error(std::string("Cannot read ") + hint + " geometry: " + importer.GetErrorString());
    }
    Mesh mesh;
    for (unsigned index = 0; index < scene->mNumMeshes; ++index) {
        const auto* source = scene->mMeshes[index];
        for (unsigned face_index = 0; face_index < source->mNumFaces; ++face_index) {
            const auto& face = source->mFaces[face_index];
            if (face.mNumIndices != 3) { continue; } // Point and line primitives are not surfaces.
            Triangle triangle;
            for (unsigned corner = 0; corner < 3; ++corner) {
                if (face.mIndices[corner] >= source->mNumVertices) {
                    throw std::runtime_error("Mesh face references a missing vertex.");
                }
                const auto point = source->mVertices[face.mIndices[corner]];
                // glTF and Assimp's normalized FBX output are Y-up; our CAD camera is Z-up.
                triangle[corner] = (format == "glb" || format == "gltf" || format == "fbx") ?
                    Vec3{point.x, -point.z, point.y} : Vec3{point.x, point.y, point.z};
            }
            append_checked_triangle(mesh, triangle);
        }
    }
    if (mesh.triangles.empty()) { throw std::runtime_error("Model contains no renderable faces; point clouds are not supported."); }
    return mesh;
}

} // namespace thumbnail
