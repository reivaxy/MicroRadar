#pragma once

#include <utility>
#include "LGFX.h"

void DrawScanLines(LGFX_Sprite& buf, const int x0, const int y0, const int x1, const int y1, const int thickness, const int trailBrightness, const int spacing, const int lineWidth = 1, const float maxRadius = -1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrt(dx * dx + dy * dy);

    // perpendicular unit vector
    float px = -dy / len;
    float py = dx / len;

    // clamps a point so it never sits further than maxRadius from the centre (x0, y0).
    // if maxRadius < 0, clamping is disabled.
    auto clamp = [&](float ex, float ey) -> std::pair<float, float> {
        if (maxRadius < 0) return { ex, ey };
        float vx = ex - x0;
        float vy = ey - y0;
        float dist = sqrt(vx * vx + vy * vy);
        if (dist <= maxRadius || dist == 0) return { ex, ey };
        float scale = maxRadius / dist;
        return { x0 + vx * scale, y0 + vy * scale };
    };

    for (int i = 0; i <= thickness; i++) {
        float t = i / (float)(thickness);
        uint8_t brightness = (uint8_t)(t * trailBrightness);

        auto [ex, ey] = clamp(x1 + (px * (i * spacing)), y1 + (py * (i * spacing)));
        buf.drawLine(x0, y0, ex, ey, lgfx::color888(0, brightness, 0));
    }

    for (int w = 0; w < lineWidth; w++) {
        float offset = w - (lineWidth - 1) / 2.0f;
        float ox = px * offset;
        float oy = py * offset;

        auto [ex, ey] = clamp(x1 + (px * (thickness * spacing)) + ox, y1 + (py * (thickness * spacing)) + oy);
        buf.drawLine(x0 + ox, y0 + oy, ex, ey, lgfx::color888(0, 200, 0));
    }
}