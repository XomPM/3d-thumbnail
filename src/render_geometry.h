#pragma once

#include "thumbnail/mesh.h"
#include "thumbnail/appearance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thumbnail {

struct RenderVertex {
    float position[3];
    float color[3];
};

inline Vec3 subtract(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

inline double dot(Vec3 left, Vec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline Vec3 cross(Vec3 left, Vec3 right) {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

inline Vec3 project(Vec3 point) {
    return {dot(point, {0.7071067812, -0.7071067812, 0}),
            dot(point, {-0.4082482905, -0.4082482905, 0.8164965809}),
            dot(point, {-0.5773502692, -0.5773502692, -0.5773502692})};
}

struct Bounds {
    Vec3 minimum{1e100, 1e100, 1e100};
    Vec3 maximum{-1e100, -1e100, -1e100};

    void include(Vec3 point) {
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
    }
    Vec3 center() const {
        return {(minimum.x + maximum.x) / 2, (minimum.y + maximum.y) / 2, (minimum.z + maximum.z) / 2};
    }
    Vec3 extent() const { return subtract(maximum, minimum); }
};

inline std::vector<RenderVertex> prepare_vertices(const Mesh& mesh, RgbColor model_color) {
    if (mesh.triangles.empty() || mesh.triangles.size() > kMaximumTriangles) {
        throw std::runtime_error("Mesh must contain between 1 and 250000 triangles.");
    }
    Bounds world;
    for (const auto& triangle : mesh.triangles) {
        for (const auto point : triangle) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
                std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)}) > 1e30) {
                throw std::runtime_error("Invalid mesh coordinates.");
            }
            world.include(point);
        }
    }
    const auto center = world.center();
    const auto extent = world.extent();
    const double scale = std::max({extent.x, extent.y, extent.z});
    if (scale <= 0) {
        throw std::runtime_error("Mesh has no spatial extent.");
    }
    const auto normalize = [center, scale](Vec3 point) {
        return Vec3{(point.x - center.x) / scale, (point.y - center.y) / scale, (point.z - center.z) / scale};
    };
    Bounds screen;
    for (const auto& triangle : mesh.triangles) {
        for (const auto point : triangle) {
            screen.include(project(normalize(point)));
        }
    }
    const auto screen_center = screen.center();
    const auto screen_extent = screen.extent();
    const double fit_scale = 1.7 / std::max(screen_extent.x, screen_extent.y);
    std::vector<RenderVertex> vertices;
    vertices.reserve(mesh.triangles.size() * 3);
    for (const auto& triangle : mesh.triangles) {
        const auto first = normalize(triangle[0]);
        const auto second = normalize(triangle[1]);
        const auto third = normalize(triangle[2]);
        auto normal = cross(subtract(second, first), subtract(third, first));
        const auto length = std::sqrt(dot(normal, normal));
        if (length <= 1e-14) {
            continue;
        }
        const double facing = dot(normal, {1, 1, 1}) < 0 ? -1.0 : 1.0;
        normal = {normal.x * facing / length, normal.y * facing / length, normal.z * facing / length};
        // Keep unlit faces distinct from the dark background while preserving shape.
        const double lighting = 0.55 + 0.45 * std::max(0.0, dot(normal, {-0.3481553119, -0.3481553119, 0.8703882798}));
        const auto shade = [lighting](std::uint8_t channel) { return static_cast<float>((channel / 255.0) * lighting); };
        for (const auto point : {first, second, third}) {
            const auto projected = project(point);
            const double depth = screen_extent.z > 1e-12 ?
                0.1 + 0.8 * (projected.z - screen.minimum.z) / screen_extent.z : 0.5;
            vertices.push_back({
                {static_cast<float>((projected.x - screen_center.x) * fit_scale),
                 static_cast<float>((projected.y - screen_center.y) * fit_scale), static_cast<float>(depth)},
                {shade(model_color.red), shade(model_color.green), shade(model_color.blue)}});
        }
    }
    if (vertices.empty()) {
        throw std::runtime_error("Mesh contains only degenerate triangles.");
    }
    return vertices;
}

} // namespace thumbnail
