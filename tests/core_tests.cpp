#include "thumbnail/renderer.h"
#include "thumbnail/stl_reader.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
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
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    require(failed, "Expected rejection, but operation succeeded.");
}

std::vector<std::byte> as_bytes(const std::string& text) {
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

constexpr char kAsciiTriangle[] =
    "solid triangle with spaces\nfacet normal 0 0 0\nouter loop\n"
    "vertex 0 0 0\nvertex 2 0 0\nvertex 0 2 0\nendloop\nendfacet\nendsolid triangle\n";

void put_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (8 * index)) & 255);
    }
}

std::vector<std::byte> binary_triangle() {
    std::vector<std::byte> bytes(134);
    std::memcpy(bytes.data(), "solid binary header", 19);
    put_u32(bytes, 80, 1);
    put_u32(bytes, 108, std::bit_cast<std::uint32_t>(2.0f));
    put_u32(bytes, 124, std::bit_cast<std::uint32_t>(2.0f));
    return bytes;
}

void check_visible(const Image& image) {
    require(image.rgba.size() == static_cast<std::size_t>(image.width) * image.height * 4, "Wrong pixel buffer size.");
    std::size_t colored = 0;
    for (std::size_t index = 0; index < image.rgba.size(); index += 4) {
        if (image.rgba[index] > 80) { ++colored; }
        require(image.rgba[index + 3] == 255, "Unexpected transparent pixels.");
    }
    require(colored > image.width * image.height / 40, "Rendered image is effectively blank.");
    require(colored < image.width * image.height * 9 / 10, "Geometry is not framed with margins.");
    require(image.rgba[0] >= 32 && image.rgba[0] <= 64, "Missing dark background or geometry touches the corner.");
}

void parser_tests() {
    const auto ascii = parse_stl(as_bytes(kAsciiTriangle));
    require(ascii.triangles.size() == 1 && ascii.triangles[0][1].x == 2, "ASCII geometry changed.");
    const auto binary = parse_stl(binary_triangle());
    require(binary.triangles.size() == 1 && binary.triangles[0][2].y == 2, "Binary geometry changed.");
    require(parse_stl(as_bytes(std::string(kAsciiTriangle) + kAsciiTriangle)).triangles.size() == 2, "Multiple solids were lost.");
    require(parse_stl(as_bytes(std::string("\xEF\xBB\xBF") + kAsciiTriangle)).triangles.size() == 1, "UTF-8 BOM not accepted.");
    require_failure([] { parse_stl({}); });
    require_failure([] { parse_stl(as_bytes("solid empty\nendsolid empty\n")); });
    require_failure([] { parse_stl(as_bytes("solid cut\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0")); });
    auto invalid = binary_triangle();
    invalid.pop_back();
    require_failure([&] { parse_stl(invalid); });
    invalid = binary_triangle();
    put_u32(invalid, 80, 0xFFFFFFFF);
    require_failure([&] { parse_stl(invalid); });
    invalid = binary_triangle();
    put_u32(invalid, 96, 0x7FC00000); // NaN vertex, not just an unreliable stored normal.
    require_failure([&] { parse_stl(invalid); });
    invalid = binary_triangle();
    invalid.push_back(std::byte{0});
    require_failure([&] { parse_stl(invalid); });
    std::vector<std::byte> too_many(84 + (kMaximumTriangles + 1) * 50);
    put_u32(too_many, 80, static_cast<std::uint32_t>(kMaximumTriangles + 1));
    require_failure([&] { parse_stl(too_many); });
}

void renderer_tests() {
    const auto mesh = parse_stl(as_bytes(kAsciiTriangle));
    check_visible(render_thumbnail(mesh, 64));
    const auto custom = render_thumbnail(mesh, 64, {{11, 22, 33}, {240, 40, 20}});
    require(custom.rgba[0] == 11 && custom.rgba[1] == 22 && custom.rgba[2] == 33, "Custom background changed.");
    unsigned red_pixels = 0;
    for (std::size_t offset = 0; offset < custom.rgba.size(); offset += 4) {
        if (custom.rgba[offset] > 100 && custom.rgba[offset + 1] < 50 && custom.rgba[offset + 2] < 35) { ++red_pixels; }
    }
    require(red_pixels > 100, "Custom model color or lighting was lost.");
    auto reversed = mesh;
    std::swap(reversed.triangles[0][1], reversed.triangles[0][2]);
    check_visible(render_thumbnail(reversed, 64));
    auto shifted = mesh;
    for (auto& point : shifted.triangles[0]) {
        point = {1e12 + point.x * 1e6, -1e12 + point.y * 1e6, 1e12};
    }
    check_visible(render_thumbnail(shifted, 256));
    require_failure([&] { render_thumbnail(mesh, 0); });
    require_failure([&] { render_thumbnail(mesh, 1025); });
    require_failure([] { render_thumbnail({}, 64); });
    require_failure([] { render_thumbnail(Mesh{{Triangle{Vec3{}, Vec3{}, Vec3{}}}}, 64); });
}

void file_tests() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::current_path() / std::filesystem::path(L"검증 파일") / unique;
    std::filesystem::create_directories(directory);
    const auto input_path = directory / L"한글 모델.stl";
    const auto output_path = directory / L"썸네일 이미지.png";
    {
        std::ofstream input(input_path, std::ios::binary);
        input << kAsciiTriangle;
    }
    const auto image = render_thumbnail(read_stl(input_path), 128);
    write_png(image, output_path);
    require(std::filesystem::file_size(input_path) == std::strlen(kAsciiTriangle), "Input was modified.");
    const auto original_size = std::filesystem::file_size(output_path);
    require(original_size > 100, "PNG output is unexpectedly short.");
    std::ifstream output(output_path, std::ios::binary);
    unsigned char header[24]{};
    output.read(reinterpret_cast<char*>(header), sizeof(header));
    require(header[0] == 137 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G', "PNG signature missing.");
    require(header[19] == 128 && header[23] == 128, "PNG dimensions differ from request.");
    output.close();
    require_failure([&] { write_png(image, output_path); });
    require(std::filesystem::file_size(output_path) == original_size, "Existing PNG was overwritten.");
    require_failure([&] { write_png(Image{128, 128, {}}, directory / "bad.png"); });
    require_failure([&] { read_stl(directory / "missing.stl"); });
    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
    std::filesystem::remove(directory);
}

} // namespace

int main() {
    unsigned failures = 0;
    const std::pair<const char*, std::function<void()>> cases[] = {
        {"ASCII/binary STL and malformed input", parser_tests},
        {"WARP rendering, framing and geometry rejection", renderer_tests},
        {"Unicode paths, PNG encoding and overwrite protection", file_tests}
    };
    for (const auto& [name, test] : cases) {
        try { test(); std::cout << "PASS: " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL: " << name << ": " << error.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
