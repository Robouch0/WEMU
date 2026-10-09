#pragma once

#include <array>
#include <cmath>
#include <utility>

namespace Core::Gfx {
    // Screen-space pixel-center coverage, independent of interpolation rounding.
    class TriangleCoverage {
        public:
            struct Point { double x, y; };
            TriangleCoverage(Point a, Point b, Point c)
            {
                const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (!std::isfinite(area) || area == 0) return;
                if (area < 0) std::swap(b, c);
                edges = {edge(a, b), edge(b, c), edge(c, a)};
                valid = true;
            }
            [[nodiscard]] bool contains(double x, double y) const
            {
                if (!valid) return false;
                for (const auto &e : edges) {
                    const double value = e.a * x + e.b * y + e.c;
                    if (!(value > 0 || (value == 0 && e.inclusive))) return false;
                }
                return true;
            }
        private:
            struct Edge { double a, b, c; bool inclusive; };
            static Edge edge(Point a, Point b)
            {
                return {a.y - b.y, b.x - a.x, a.x * b.y - b.x * a.y,
                    b.y < a.y || (b.y == a.y && b.x > a.x)};
            }
            std::array<Edge, 3> edges{};
            bool valid{};
    };
}
