#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace Core::Gfx {

    // Latte CUBE sampling consumes S/T in [1,2] and a float face ID. The
    // reference fetch returns a texel from one of six Vulkan-order cube faces.
    inline std::array<float, 3> cubeDirection(unsigned face, float u, float v)
    {
        const float x = u * 2 - 1, y = v * 2 - 1;
        switch (face) {
            case 0:
                return {1, -y, -x};
            case 1:
                return {-1, -y, x};
            case 2:
                return {x, 1, y};
            case 3:
                return {x, -1, -y};
            case 4:
                return {x, -y, 1};
            default:
                return {-x, -y, -1};
        }
    }
    inline std::array<float, 3> cubeCoordinates(const std::array<float, 3> &direction)
    {
        const auto [x, y, z] = direction;
        const float ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
        float s{}, t{}, major{}, face{};
        if (az >= ax && az >= ay) {
            s = z < 0 ? -x : x;
            t = -y;
            major = az;
            face = z < 0 ? 5.f : 4.f;
        } else if (ay >= ax) {
            s = x;
            t = y < 0 ? -z : z;
            major = ay;
            face = y < 0 ? 3.f : 2.f;
        } else {
            s = x < 0 ? z : -z;
            t = -y;
            major = ax;
            face = x < 0 ? 1.f : 0.f;
        }
        return {s / major * .5f + .5f, t / major * .5f + .5f, face};
    }

    template<class Fetch>
    std::array<float, 4> sampleCube(unsigned width, float s, float t, unsigned face, bool linear, const Fetch &fetch)
    {
        const auto coordinate = cubeCoordinates(cubeDirection(face, s - 1.f, t - 1.f));
        face = unsigned(coordinate[2]);
        const auto nearest = [&](unsigned sourceFace, float u, float v) {
            const auto p = cubeCoordinates(cubeDirection(sourceFace, u, v));
            const auto x = unsigned(std::clamp(std::floor(double(p[0]) * width), 0.0, double(width - 1)));
            const auto y = unsigned(std::clamp(std::floor(double(p[1]) * width), 0.0, double(width - 1)));
            return fetch(unsigned(p[2]), x, y);
        };
        if (!linear)
            return nearest(face, coordinate[0], coordinate[1]);
        const double px = double(coordinate[0]) * width - .5, py = double(coordinate[1]) * width - .5;
        const auto x = int(std::floor(px)), y = int(std::floor(py));
        const float fx = float(px - x), fy = float(py - y);
        const auto tap = [&](int tx, int ty) {
            const float u = (float(tx) + .5f) / float(width), v = (float(ty) + .5f) / float(width);
            if ((tx < 0 || tx >= int(width)) && (ty < 0 || ty >= int(width))) {
                // The missing corner tap averages the three incident face texels.
                const float cu = (float(std::clamp(tx, 0, int(width) - 1)) + .5f) / float(width);
                const float cv = (float(std::clamp(ty, 0, int(width) - 1)) + .5f) / float(width);
                const auto a = nearest(face, cu, cv), b = nearest(face, u, cv), c = nearest(face, cu, v);
                std::array<float, 4> result{};
                for (unsigned channel = 0; channel < 4; ++channel)
                    result[channel] = (a[channel] + b[channel] + c[channel]) / 3.f;
                return result;
            }
            return nearest(face, u, v);
        };
        const auto a = tap(x, y), b = tap(x + 1, y), c = tap(x, y + 1), d = tap(x + 1, y + 1);
        std::array<float, 4> result{};
        for (unsigned channel = 0; channel < 4; ++channel)
            result[channel] = std::lerp(std::lerp(a[channel], b[channel], fx), std::lerp(c[channel], d[channel], fx), fy);
        return result;
    }
} // namespace Core::Gfx
