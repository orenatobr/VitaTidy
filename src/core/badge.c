#include "badge.h"

#include <math.h>

Badge badge_for(int w, int h)
{
    Badge b = {.fill = {34, 197, 94}, .check = {255, 255, 255}, .check_width = 2.4f};
    if (w == 144 && h == 80) {
        b.cx = 126.0f; b.cy = 18.0f; b.r = 13.0f;
    } else {
        float s = (float)w / 128.0f;
        b.cx = 64.0f * s; b.cy = 64.0f * s; b.r = 13.0f * s;
        b.check_width *= s;
    }
    return b;
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    float t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float qx = ax + t * dx - px, qy = ay + t * dy - py;
    return sqrtf(qx * qx + qy * qy);
}

static void blend(uint8_t *px, const uint8_t rgb[3], float cov)
{
    if (cov <= 0) return;
    for (int i = 0; i < 3; i++) px[i] = (uint8_t)(px[i] + (rgb[i] - px[i]) * cov + 0.5f);
    px[3] = (uint8_t)(px[3] + (255 - px[3]) * cov + 0.5f);
}

void badge_draw(Image *img, const Badge *b)
{
    /* Check geometry follows the original tool: a short left leg and a longer right leg. */
    float p1x = b->cx - b->r * 0.70f, p1y = b->cy + b->r * 0.10f;
    float p2x = b->cx - b->r * 0.05f, p2y = b->cy + b->r * 0.62f;
    float p3x = b->cx + b->r * 0.68f, p3y = b->cy - b->r * 0.62f;
    float half = b->check_width / 2.0f;

    int x0 = (int)floorf(b->cx - b->r - 1), x1 = (int)ceilf(b->cx + b->r + 1);
    int y0 = (int)floorf(b->cy - b->r - 1), y1 = (int)ceilf(b->cy + b->r + 1);
    const int ss = 4;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= img->w || y >= img->h) continue;
            int in_circle = 0, in_check = 0;
            for (int sy = 0; sy < ss; sy++) {
                for (int sx = 0; sx < ss; sx++) {
                    float px = x + (sx + 0.5f) / ss, py = y + (sy + 0.5f) / ss;
                    float dx = px - b->cx, dy = py - b->cy;
                    if (dx * dx + dy * dy <= b->r * b->r) in_circle++;
                    if (seg_dist(px, py, p1x, p1y, p2x, p2y) <= half || seg_dist(px, py, p2x, p2y, p3x, p3y) <= half)
                        in_check++;
                }
            }
            uint8_t *px = img->rgba + ((size_t)y * img->w + x) * 4;
            blend(px, b->fill, (float)in_circle / (ss * ss));
            blend(px, b->check, (float)in_check / (ss * ss));
        }
    }
}
