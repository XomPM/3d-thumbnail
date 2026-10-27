#include "thumbnail/model_reader.h"
#include "zip_limits.h"

#include <lib3mf_implicit.hpp>
#include <array>
#include <set>
#include <stdexcept>

namespace thumbnail {
namespace {

using Matrix = std::array<double, 16>;
constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

Matrix matrix_from(const Lib3MF::sTransform& transform) {
    Matrix result = kIdentity;
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            result[row * 4 + column] = transform.m_Fields[column][row];
        }
    }
    return result;
}

Matrix multiply(const Matrix& parent, const Matrix& child) {
    Matrix result{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            for (unsigned axis = 0; axis < 4; ++axis) {
                result[row * 4 + column] += parent[row * 4 + axis] * child[axis * 4 + column];
            }
        }
    }
    return result;
}

Vec3 transform_position(const Lib3MF::sPosition& point, const Matrix& matrix) {
    const double x = point.m_Coordinates[0], y = point.m_Coordinates[1], z = point.m_Coordinates[2];
    return {matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
            matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
            matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11]};
}

void append_mesh(Mesh& mesh, const Lib3MF::PMeshObject& source, const Matrix& transform) {
    const auto vertex_count = source->GetVertexCount();
    const auto triangle_count = source->GetTriangleCount();
    if (vertex_count > kMaximumTriangles * 3 || triangle_count > kMaximumTriangles - mesh.triangles.size()) {
        throw std::runtime_error("3MF exceeds the mesh size limit.");
    }
    std::vector<Lib3MF::sPosition> vertices;
    std::vector<Lib3MF::sTriangle> indices;
    source->GetVertices(vertices);
    source->GetTriangleIndices(indices);
    for (const auto& face : indices) {
        Triangle triangle;
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto index = face.m_Indices[corner];
            if (index >= vertices.size()) { throw std::runtime_error("Invalid 3MF vertex index."); }
            triangle[corner] = transform_position(vertices[index], transform);
        }
        append_checked_triangle(mesh, triangle);
    }
}

struct Traversal {
    Lib3MF::PModel model;
    Mesh mesh;
    std::set<Lib3MF_uint32> ancestors;
    unsigned visits{};

    void append(const Lib3MF::PObject& object, const Matrix& transform, unsigned depth) {
        const auto id = object->GetUniqueResourceID();
        if (depth > 64 || ++visits > 10000 || !ancestors.insert(id).second) {
            throw std::runtime_error("3MF component graph is cyclic or too complex.");
        }
        if (object->IsMeshObject()) {
            append_mesh(mesh, model->GetMeshObjectByID(id), transform);
        } else if (object->IsComponentsObject()) {
            const auto components = model->GetComponentsObjectByID(id);
            const auto count = components->GetComponentCount();
            if (count > 10000) { throw std::runtime_error("3MF has too many component instances."); }
            for (Lib3MF_uint32 index = 0; index < count; ++index) {
                const auto component = components->GetComponent(index);
                const auto child = component->HasTransform() ? matrix_from(component->GetTransform()) : kIdentity;
                append(component->GetObjectResource(), multiply(transform, child), depth + 1);
            }
        } else {
            throw std::runtime_error("3MF object has no supported mesh representation.");
        }
        ancestors.erase(id);
    }
};

} // namespace

Mesh parse_3mf(std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > kMaximumFileBytes) {
        throw std::runtime_error("3MF must be nonempty and no larger than 64 MiB.");
    }
    validate_3mf_zip(bytes);
    try {
        const auto wrapper = Lib3MF::CWrapper::loadLibrary();
        const auto model = wrapper->CreateModel();
        const auto reader = model->QueryReader("3mf");
        // Slicers (Bambu Studio, PrusaSlicer, ...) add vendor metadata and attributes that strict mode
        // rejects. Non-strict mode records them as warnings; malformed structure still throws and every
        // triangle is checked again by append_mesh/append_checked_triangle.
        reader->SetStrictModeActive(false);
        reader->ReadFromBuffer(Lib3MF::CInputVector<Lib3MF_uint8>(
            reinterpret_cast<const Lib3MF_uint8*>(bytes.data()), bytes.size()));
        Traversal traversal{model, {}, {}, 0};
        const auto items = model->GetBuildItems();
        while (items->MoveNext()) {
            const auto item = items->GetCurrent();
            const auto transform = item->HasObjectTransform() ? matrix_from(item->GetObjectTransform()) : kIdentity;
            traversal.append(item->GetObjectResource(), transform, 0);
        }
        if (traversal.mesh.triangles.empty()) { throw std::runtime_error("3MF build contains no mesh triangles."); }
        return std::move(traversal.mesh);
    } catch (const Lib3MF::ELib3MFException& failure) {
        throw std::runtime_error(std::string("3MF conversion failed: ") + failure.what());
    }
}

} // namespace thumbnail
