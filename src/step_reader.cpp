#include "thumbnail/model_reader.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <DESTEP_Parameters.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <memory>

namespace thumbnail {
namespace {

double meshing_deflection(const TopoDS_Shape& shape) {
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds, false);
    if (bounds.IsVoid() || bounds.IsOpen()) { throw std::runtime_error("STEP has no finite bounds."); }
    double x0, y0, z0, x1, y1, z1;
    bounds.Get(x0, y0, z0, x1, y1, z1);
    const double size = std::max({x1 - x0, y1 - y0, z1 - z0});
    if (!std::isfinite(size) || size <= 0 || size > 1e20) {
        throw std::runtime_error("STEP geometry size is outside the supported range.");
    }
    return std::max(1e-6, size * 0.002);
}

Mesh triangulate(const TopoDS_Shape& shape) {
    BRepMesh_IncrementalMesh mesher(shape, meshing_deflection(shape), false, 0.35, false);
    if (!mesher.IsDone()) { throw std::runtime_error("STEP tessellation failed."); }
    Mesh mesh;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const auto face = TopoDS::Face(faces.Current());
        TopLoc_Location location;
        const auto tessellation = BRep_Tool::Triangulation(face, location);
        if (tessellation.IsNull()) { throw std::runtime_error("A STEP face could not be tessellated."); }
        if (static_cast<std::size_t>(tessellation->NbTriangles()) > kMaximumTriangles - mesh.triangles.size()) {
            throw std::runtime_error("STEP exceeds the 250000 triangle limit.");
        }
        for (int index = 1; index <= tessellation->NbTriangles(); ++index) {
            int a, b, c;
            tessellation->Triangle(index).Get(a, b, c);
            if (face.Orientation() == TopAbs_REVERSED) { std::swap(b, c); }
            Triangle triangle;
            const int corners[] = {a, b, c};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto point = tessellation->Node(corners[corner]).Transformed(location.Transformation());
                triangle[corner] = {point.X(), point.Y(), point.Z()};
            }
            append_checked_triangle(mesh, triangle);
        }
    }
    if (mesh.triangles.empty()) { throw std::runtime_error("STEP contains no renderable faces."); }
    return mesh;
}

} // namespace

Mesh parse_step(std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > kMaximumFileBytes) {
        throw std::runtime_error("STEP must be nonempty and no larger than 64 MiB.");
    }
    try {
        std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        STEPControl_Reader reader;
        const DESTEP_Parameters parameters;
        if (reader.ReadStream("thumbnail.step", parameters, input) != IFSelect_RetDone) {
            throw std::runtime_error("Cannot parse STEP model.");
        }
        const auto roots = reader.NbRootsForTransfer();
        if (roots <= 0 || roots > 10000) { throw std::runtime_error("Unsupported STEP root count."); }
        if (reader.TransferRoots() != roots) { throw std::runtime_error("Some STEP roots could not be converted."); }
        const auto shape = reader.OneShape();
        if (shape.IsNull()) { throw std::runtime_error("STEP contains no geometric shape."); }
        return triangulate(shape);
    } catch (const Standard_Failure& failure) {
        const auto message = failure.GetMessageString();
        throw std::runtime_error(std::string("STEP conversion failed: ") + (message ? message : "OCCT error"));
    }
}

Mesh read_iges(const std::filesystem::path& path) {
    struct CloseFile { void operator()(void* handle) const { if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); } } };
    const std::unique_ptr<void, CloseFile> file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    LARGE_INTEGER length{};
    if (file.get() == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.get(), &length) || length.QuadPart <= 0 ||
        length.QuadPart > kMaximumFileBytes) { throw std::runtime_error("IGES must be readable, nonempty and no larger than 64 MiB."); }
    try {
        // OCCT 7.9's IGES controller has no stream reader. The Shell bridge already denies other writers.
        // Share-write permits its existing read/write copy handle; this handle itself is read-only.
        IGESControl_Reader reader;
        const auto utf8 = path.u8string();
        const std::string filename(utf8.begin(), utf8.end());
        if (reader.ReadFile(filename.c_str()) != IFSelect_RetDone) {
            throw std::runtime_error("Cannot parse IGES model.");
        }
        const auto roots = reader.NbRootsForTransfer();
        if (roots <= 0 || roots > 10000) { throw std::runtime_error("Unsupported IGES root count."); }
        if (reader.TransferRoots() != roots) { throw std::runtime_error("Some IGES roots could not be converted."); }
        const auto shape = reader.OneShape();
        if (shape.IsNull()) { throw std::runtime_error("IGES contains no geometric shape."); }
        return triangulate(shape);
    } catch (const Standard_Failure& failure) {
        const auto message = failure.GetMessageString();
        throw std::runtime_error(std::string("IGES conversion failed: ") + (message ? message : "OCCT error"));
    }
}

} // namespace thumbnail
