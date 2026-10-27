#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace thumbnail {

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

using Triangle = std::array<Vec3, 3>;

struct Mesh {
    std::vector<Triangle> triangles;
};

inline constexpr std::size_t kMaximumTriangles = 250'000;
inline constexpr std::size_t kMaximumFileBytes = 64 * 1024 * 1024;

} // namespace thumbnail
