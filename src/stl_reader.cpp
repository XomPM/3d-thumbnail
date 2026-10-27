#include "thumbnail/stl_reader.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace thumbnail {
namespace {

std::uint32_t read_u32(const std::byte* data) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(data[index]) << (8 * index);
    }
    return value;
}

double read_float(const std::byte* data) {
    return static_cast<double>(std::bit_cast<float>(read_u32(data)));
}

void append_triangle(Mesh& mesh, const Triangle& triangle) {
    if (mesh.triangles.size() >= kMaximumTriangles) {
        throw std::runtime_error("Triangle limit exceeded (250000 in this prototype).");
    }
    for (const auto& point : triangle) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            std::abs(point.x) > 1e30 || std::abs(point.y) > 1e30 || std::abs(point.z) > 1e30) {
            throw std::runtime_error("STL contains invalid or excessively large coordinates.");
        }
    }
    mesh.triangles.push_back(triangle);
}

Mesh parse_binary(std::span<const std::byte> bytes, std::uint32_t count) {
    if (count > kMaximumTriangles) {
        throw std::runtime_error("Triangle limit exceeded (250000 in this prototype).");
    }
    Mesh mesh;
    mesh.triangles.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* record = bytes.data() + 84 + static_cast<std::size_t>(index) * 50;
        Triangle triangle;
        // Recalculate normals for rendering; supplied STL normals are often missing.
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto* position = record + 12 + corner * 12;
            triangle[corner] = {read_float(position), read_float(position + 4), read_float(position + 8)};
        }
        append_triangle(mesh, triangle);
    }
    return mesh;
}

void expect_token(std::istream& input, const char* expected) {
    std::string actual;
    if (!(input >> actual) || actual != expected) {
        throw std::runtime_error(std::string("Invalid ASCII STL: expected ") + expected + ".");
    }
}

Vec3 read_position(std::istream& input) {
    Vec3 position;
    if (!(input >> position.x >> position.y >> position.z)) {
        throw std::runtime_error("Invalid ASCII STL numeric value.");
    }
    return position;
}

Mesh parse_ascii(std::span<const std::byte> bytes) {
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.erase(0, 3);
    }
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    Mesh mesh;
    std::string token;
    while (input >> token) {
        if (token != "solid") {
            throw std::runtime_error("Invalid STL: expected an ASCII solid or an exact binary record count.");
        }
        std::getline(input, token); // The solid name can contain spaces.
        bool closed = false;
        while (input >> token) {
            if (token == "endsolid") {
                std::getline(input, token);
                closed = true;
                break;
            }
            if (token != "facet") {
                throw std::runtime_error("Invalid ASCII STL: expected facet or endsolid.");
            }
            expect_token(input, "normal");
            read_position(input);
            expect_token(input, "outer");
            expect_token(input, "loop");
            Triangle triangle;
            for (auto& point : triangle) {
                expect_token(input, "vertex");
                point = read_position(input);
            }
            expect_token(input, "endloop");
            expect_token(input, "endfacet");
            append_triangle(mesh, triangle);
        }
        if (!closed) {
            throw std::runtime_error("Truncated ASCII STL: missing endsolid.");
        }
    }
    return mesh;
}

} // namespace

Mesh parse_stl(std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > kMaximumFileBytes) {
        throw std::runtime_error("STL must be nonempty and no larger than 64 MiB.");
    }
    Mesh mesh;
    if (bytes.size() >= 84) {
        const auto count = read_u32(bytes.data() + 80);
        const auto expected_size = 84ULL + static_cast<std::uint64_t>(count) * 50;
        if (expected_size == bytes.size()) {
            mesh = parse_binary(bytes, count);
        } else {
            mesh = parse_ascii(bytes);
        }
    } else {
        mesh = parse_ascii(bytes);
    }
    if (mesh.triangles.empty()) {
        throw std::runtime_error("STL contains no triangles.");
    }
    return mesh;
}

Mesh read_stl(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open input STL.");
    }
    const auto length = input.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(kMaximumFileBytes)) {
        throw std::runtime_error("STL must be nonempty and no larger than 64 MiB.");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length))) {
        throw std::runtime_error("Cannot read the complete input STL.");
    }
    return parse_stl(bytes);
}

} // namespace thumbnail
