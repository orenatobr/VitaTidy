/* The green "completed" badge drawn over game icons. */
#ifndef VT_BADGE_H
#define VT_BADGE_H

#include "pngio.h"

typedef struct {
    float cx, cy, r;      /* circle center and radius in pixels */
    uint8_t fill[3];      /* circle color */
    uint8_t check[3];     /* checkmark color */
    float check_width;    /* stroke width in pixels */
} Badge;

/* Geometry for an icon size: PSP ICON0 (144x80) gets a corner badge, others a centered one. */
Badge badge_for(int w, int h);
/* Draws the badge with anti-aliasing; transparent pixels under it become opaque. */
void badge_draw(Image *img, const Badge *b);

#endif
