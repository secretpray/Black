#ifndef BLACK_HUD_LAYOUT_H
#define BLACK_HUD_LAYOUT_H
#include <math.h>

/* BLACK draws its 2D at 640x480 and the presenter shows it as wide as the
 * output. Following TGP's src/game/hud.cpp, a screen-space point keeps a 4:3
 * shape by x' = x * narrow + anchor * spare width, narrow = (4/3) / aspect. */
#define BLACK_HUD_OFF -1.0f
#define BLACK_HUD_CENTRE 0.5f

static inline float black_hud_narrow(double aspect)
{
    return (float)(4.0 / 3.0 / aspect);
}

/* View width: the game's 1 on 4:3 and 1.2 on 16:9; past that the 16:9 height
 * of view is kept. */
static inline float black_view_width(float narrow)
{
    float wider = 0.4f + 0.6f / narrow, taller = 0.9f / narrow;
    return wider > taller ? wider : taller;
}

/* How far in the 640 Flash stage sits on a widescreen. */
static inline float black_stage_x(float narrow)
{
    return 320.0f * (1.0f / narrow - 1.0f);
}

/* A point an emitter draws at x + offset, on a target width wide, anchored at
 * share `anchor` of the spare width (0 left, 0.5 centre, 1 right). Centred, a
 * point on or past a screen side stays: full-screen rectangles keep covering. */
static inline float black_hud_x(float x, float offset, float anchor, float width, float narrow)
{
    float at = x + offset;
    if (anchor == BLACK_HUD_CENTRE && (at <= 0.0f || at >= width)) return x;
    return at * narrow + anchor * width * (1.0f - narrow) - offset;
}

/* Upscaled, a sprite's outer pixels sample past its edge texels and blend in
 * its neighbours in the HUD texture; pull its uv in to where 480p samples. */
static inline float black_hud_inset(int scale)
{
    float inset = 0.5f - 0.5f / (float)(scale > 0 ? scale : 1);
    return inset > 0.0f ? inset : 0.0f;
}

static inline float black_pull_in(float from, float to, float length, float inset)
{
    return length > 0.0f ? from + (to - from) / length * inset : from;
}

/* Two corners per quad: scaled about the middle on axis 0 (x) or 1 (y). */
static inline void black_scale_quad(float *q, int axis, float scale)
{
    float middle = (q[axis] + q[axis + 2]) / 2.0f;
    q[axis] = middle + (q[axis] - middle) * scale;
    q[axis + 2] = middle + (q[axis + 2] - middle) * scale;
}
#endif
