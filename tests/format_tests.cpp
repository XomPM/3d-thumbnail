#include "thumbnail/model_reader.h"
#include "thumbnail/renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace thumbnail;

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

void require_failure(const std::function<void()>& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Malformed input was accepted.");
}

std::vector<std::byte> bytes_of(const std::string& text) {
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::array<double, 6> bounds_of(const Mesh& mesh) {
    const double infinity = std::numeric_limits<double>::infinity();
    std::array<double, 6> result{infinity, infinity, infinity, -infinity, -infinity, -infinity};
    for (const auto& triangle : mesh.triangles) {
        for (const auto& point : triangle) {
            const double values[]{point.x, point.y, point.z};
            for (unsigned axis = 0; axis < 3; ++axis) {
                result[axis] = std::min(result[axis], values[axis]);
                result[axis + 3] = std::max(result[axis + 3], values[axis]);
            }
        }
    }
    return result;
}

void expect_bounds(const Mesh& mesh, const std::array<double, 6>& expected) {
    const auto actual = bounds_of(mesh);
    for (unsigned index = 0; index < actual.size(); ++index) {
        if (std::abs(actual[index] - expected[index]) >= 0.01) {
            std::cerr << "Bounds:";
            for (auto value : actual) { std::cerr << ' ' << value; }
            std::cerr << '\n';
            throw std::runtime_error("Unexpected model bounds or placement.");
        }
    }
}

void check_visible(const Mesh& mesh) {
    const auto image = render_thumbnail(mesh, 128);
    std::size_t colored = 0;
    for (std::size_t offset = 0; offset < image.rgba.size(); offset += 4) {
        if (image.rgba[offset] > 80) { ++colored; }
    }
    require(colored > 200 && colored < 15000, "Rendered model is blank or clipped.");
}

void required_formats(const std::filesystem::path& samples, const std::filesystem::path& temporary) {
    const char* files[]{"bearing_binary.stl", "bearing_ascii.stl", "bearing.obj", "bearing.3mf",
                        "bearing_ap203.stp", "bearing_ap214.step", "bearing_ap242.step"};
    for (const auto* name : files) {
        const auto path = samples / name;
        const auto original = read_model_bytes(path);
        const auto mesh = read_model(path);
        require(mesh.triangles.size() >= 100, "Bearing geometry was lost.");
        expect_bounds(mesh, {-30, -22, 0, 30, 22, 30});
        check_visible(mesh);
        require(read_model_bytes(path) == original, "Reader modified its input file.");
        const auto unicode = temporary / (L"한글 부품 " + std::filesystem::path(name).wstring());
        std::filesystem::copy_file(path, unicode);
        expect_bounds(read_model(unicode), {-30, -22, 0, 30, 22, 30});
        std::cout << "PASS format and Unicode path: " << name << '\n';
    }
    const auto placed = read_model(samples / "placed_solids.step");
    expect_bounds(placed, {-30, -22, 0, 130, 42, 30});
    Mesh first, second;
    for (const auto& face : placed.triangles) {
        const auto center_x = (face[0].x + face[1].x + face[2].x) / 3;
        (center_x < 50 ? first : second).triangles.push_back(face);
    }
    expect_bounds(first, {-30, -22, 0, 30, 22, 30});
    expect_bounds(second, {70, -2, 0, 130, 42, 30});
    require(first.triangles.size() == second.triangles.size(), "STEP instance tessellation differs.");
    const auto nested = read_model(samples / "nested_instances.3mf");
    require(nested.triangles.size() == 8, "3MF instance or build filtering failed.");
    expect_bounds(nested, {98, 10, 0, 150, 12, 2});
    std::cout << "PASS STEP placement and 3MF nested rotations, translations, repeated instances\n";
}

void obj_cases() {
    // A concave L-shaped polygon has area 3, unlike an incorrect triangle fan.
    const auto mesh = parse_obj(bytes_of("mtllib missing-material.mtl\n"
        "v 0 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nv 1 2 0\nv 0 2 0\nf -6 -5 -4 -3 -2 -1\n"));
    require(mesh.triangles.size() == 4, "OBJ polygon was not triangulated.");
    double area = 0;
    for (const auto& triangle : mesh.triangles) {
        const auto& a = triangle[0]; const auto& b = triangle[1]; const auto& c = triangle[2];
        area += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
    }
    require(std::abs(area - 3) < 1e-6, "Concave OBJ polygon triangulation has wrong area.");
    require_failure([] { parse_obj({}); });
    require_failure([] { parse_obj(bytes_of("v 0 0 0\nv 1 0 0\nf 1 2 999\n")); });
    require_failure([] { parse_obj(bytes_of("v nan 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")); });
    require_failure([] { parse_obj(bytes_of("v 0 0 0\nv 1 0 0\nl 1 2\n")); });
    std::cout << "PASS OBJ concave polygons, negative indices and invalid input\n";
}

void expanded_formats(const std::filesystem::path& samples, const std::filesystem::path& temporary) {
    std::filesystem::copy_file(samples / "bearing.bin", temporary / "bearing.bin");
    for (const auto* name : {"bearing.ply", "bearing_ascii.ply", "bearing.glb", "bearing.gltf", "bearing_embedded.gltf",
            "bearing.fbx", "bearing_ascii.fbx", "bearing.iges", "bearing_brep.igs"}) {
        const auto path = samples / name;
        const auto original = read_model_bytes(path);
        const auto mesh = read_model(path);
        expect_bounds(mesh, {-30, -22, 0, 30, 22, 30});
        require(mesh.triangles.size() >= 100, "Expanded format lost model geometry.");
        check_visible(mesh);
        require(read_model_bytes(path) == original, "Expanded format modified its input.");
        const auto unicode = temporary / (L"확장 형식 " + std::filesystem::path(name).wstring());
        std::filesystem::copy_file(path, unicode);
        expect_bounds(read_model(unicode), {-30, -22, 0, 30, 22, 30});
        std::cout << "PASS expanded format and Unicode: " << name << '\n';
    }
    const auto placed = read_model(samples / "placed_meshes.glb");
    require(placed.triangles.size() == 1048, "glTF node instances lost.");
    expect_bounds(placed, {-30, -22, 0, 130, 42, 30});
    for (const auto* format : {"ply", "glb", "gltf", "fbx"}) {
        require_failure([&] { parse_mesh(bytes_of("broken model"), format); });
    }
    { std::ofstream broken(temporary / "broken.iges"); broken << "broken IGES"; }
    require_failure([&] { read_model(temporary / "broken.iges"); });
    require_failure([] { parse_mesh(bytes_of("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n0 0 0\n"), "ply"); });

    const auto original_bytes = read_model_bytes(samples / "bearing.gltf");
    const std::string original(reinterpret_cast<const char*>(original_bytes.data()), original_bytes.size());
    auto with_uri = [&](const std::string& uri) {
        auto text = original;
        const auto position = text.find("bearing.bin");
        require(position != std::string::npos, "glTF test URI missing.");
        text.replace(position, std::strlen("bearing.bin"), uri);
        return bytes_of(text);
    };
    for (const auto& uri : {"../bearing.bin", "%2e%2e/bearing.bin", "/bearing.bin", "file:///C:/bearing.bin",
            "https://example.invalid/bearing.bin", "bearing.bin:stream", "missing.bin", "bearing%00.bin", "bad%xx.bin"}) {
        require_failure([&] { parse_mesh(with_uri(uri), "gltf", temporary); });
    }
    std::filesystem::create_directories(temporary / L"자료");
    std::filesystem::copy_file(samples / "bearing.bin", temporary / L"자료" / L"부품.bin");
    expect_bounds(parse_mesh(with_uri("%EC%9E%90%EB%A3%8C/%EB%B6%80%ED%92%88.bin"), "gltf", temporary), {-30, -22, 0, 30, 22, 30});
    require_failure([&] { parse_mesh(original_bytes, "gltf"); });
    {
        std::ofstream oversized(temporary / "oversized.bin", std::ios::binary);
        oversized.seekp(static_cast<std::streamoff>(kMaximumFileBytes));
        oversized.put('x');
    }
    require_failure([&] { parse_mesh(with_uri("oversized.bin"), "gltf", temporary); });
    auto truncated = read_model_bytes(samples / "bearing.glb");
    truncated.resize(20);
    require_failure([&] { parse_mesh(truncated, "glb"); });
    std::cout << "PASS glTF placement, external buffer containment/limits, malformed files and point-cloud rejection\n";
}

void malformed_cases(const std::filesystem::path& samples) {
    require_failure([] { parse_step({}); });
    require_failure([] { parse_step(bytes_of("ISO-10303-21;\nDATA;\n#1=broken")); });
    require_failure([] { parse_3mf({}); });
    require_failure([] { parse_3mf(bytes_of("PK not a ZIP archive")); });
    require_failure([&] { read_model(samples / "empty_build.3mf"); });
    auto package = read_model_bytes(samples / "bearing.3mf");
    package.resize(package.size() - 10);
    require_failure([&] { parse_3mf(package); });
    package = read_model_bytes(samples / "bearing.3mf");
    const std::array<std::byte, 4> signature{std::byte{0x50}, std::byte{0x4b}, std::byte{0x01}, std::byte{0x02}};
    const auto found = std::search(package.begin(), package.end(), signature.begin(), signature.end());
    require(found != package.end(), "Fixture has no ZIP directory.");
    const auto central = static_cast<std::size_t>(found - package.begin());
    // Claim a 2 GiB entry without allocating or decompressing one.
    for (unsigned index = 0; index < 4; ++index) { package[central + 24 + index] = std::byte{0}; }
    package[central + 27] = std::byte{0x80};
    require_failure([&] { parse_3mf(package); });
    require_failure([&] { read_model(samples / "unsupported.fbx"); });
    std::cout << "PASS malformed STEP/3MF and ZIP expansion limit\n";
}
}

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path temporary;
    try {
        require(argc == 3, "Usage: format-tests <samples> <test-output>");
        temporary = std::filesystem::path(argv[2]) / (L"형식 검증 " +
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(!std::filesystem::exists(temporary), "Test directory already exists; inspect previous failure first.");
        std::filesystem::create_directories(temporary);
        required_formats(argv[1], temporary);
        expanded_formats(argv[1], temporary);
        obj_cases();
        malformed_cases(argv[1]);
        std::filesystem::remove_all(temporary);
        return 0;
    } catch (const std::exception& error) {
        // Keep failed-test fixtures available for diagnosis.
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
