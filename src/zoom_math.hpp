// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace smooth_zoom {
struct View {
    double zoom = 1.0;
    double x = 0.0;
    double y = 0.0;
};

inline View step(View from, double factor, double uvX, double uvY, double maxZoom)
{
    View to;
    to.zoom = std::clamp(from.zoom * factor, 1.0, std::max(1.0, maxZoom));
    if (to.zoom <= 1.0000001) return {};
    // Content under the pointer stays under the pointer after the zoom.
    const double originalX = from.x + uvX / from.zoom;
    const double originalY = from.y + uvY / from.zoom;
    to.x = std::clamp(originalX - uvX / to.zoom, 0.0, 1.0 - 1.0 / to.zoom);
    to.y = std::clamp(originalY - uvY / to.zoom, 0.0, 1.0 - 1.0 / to.zoom);
    return to;
}

inline View advance(View from, View target, double dt, int durationMs)
{
    if (durationMs <= 0) return target;
    if (dt <= 0.0) return from;
    // Exponential easing: roughly 99% of the change within durationMs.
    const double alpha = 1.0 - std::exp(-4.605170186 * std::clamp(dt, 0.0, 0.25) * 1000.0 / durationMs);
    auto lerp = [alpha](double a, double b) { return a + (b - a) * alpha; };
    return {lerp(from.zoom, target.zoom), lerp(from.x, target.x), lerp(from.y, target.y)};
}
}
