/* Research trace of the HUD layer's sprites (reticle_trace.h), not a feature:
 * BLACK_RETICLE_TRACE=1 logs one line per distinct uv and size, to learn which
 * sub-rect of the 512x256 HUD atlas the game draws for a weapon's aiming mark
 * and for the other shapes in it. A sprite is logged when it is (a) within
 * 80 HUD pixels of the frame's centre and at most 96 pixels across, (b) the
 * aiming dot's sub-rect wherever it is drawn, (c) a larger sprite covering the
 * centre, such as a scope overlay (the full-frame film quad left out), or (d)
 * any 32x32 cell of the atlas grid. At most 256 lines, a heartbeat every 5 s
 * while such sprites are drawn, and the first batch sizes. Silent and a single
 * cached getenv when the variable is unset. The uv are logged as they come;
 * the texel column assumes the 512x256 atlas and is there to read by eye. */
#include "recomp_types.h"
#include "reticle_trace.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double trace_start;

static int reticle_trace(void)
{
    static int on = -1;
    if (on < 0) {
        struct timespec now;
        on = getenv("BLACK_RETICLE_TRACE") != NULL;
        clock_gettime(CLOCK_MONOTONIC, &now);
        trace_start = now.tv_sec + now.tv_nsec / 1e9;
    }
    return on;
}

typedef struct { float u0, v0, u1, v1, w, h; } TracedSprite;

static void reticle_trace_one(float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1, const char *kind)
{
    static TracedSprite seen[256];
    static unsigned count;
    float under = fminf(u0, u1), over = fmaxf(u0, u1);
    float top = fminf(v0, v1), bottom = fmaxf(v0, v1);
    float w = fabsf(x1 - x0), h = fabsf(y1 - y0);
    float left = fminf(x0, x1), right = fmaxf(x0, x1);
    float upper = fminf(y0, y1), lower = fmaxf(y0, y1);
    float cx = (left + right) * 0.5f, cy = (upper + lower) * 0.5f;
    int middle = fabsf(cx - 320.0f) <= 80.0f && fabsf(cy - 240.0f) <= 80.0f && w <= 96.0f && h <= 96.0f;
    /* The aiming dot's sub-rect, for a draw that is not at the middle. */
    int dot = under > 0.24f && over < 0.32f && top > 0.62f && bottom < 0.75f;
    /* A larger sprite centred on the reticle: the sniper scope's overlay. The
     * film quad also covers the middle, so the whole-atlas rect is left out. */
    int cover = left <= 320.0f && right >= 320.0f && upper <= 240.0f && lower >= 240.0f &&
        w <= 1280.0f && h <= 960.0f &&
        !(under < 0.01f && over > 0.99f && top < 0.01f && bottom > 0.99f);
    /* Any 32x32 cell of the 512x256 atlas, wherever it is drawn: the same atlas
     * holds the rings, chevrons, spread scales and soldier marks, and the
     * damage chevrons sit at the frame's edge, away from the crosshair. The
     * game's sub-rects land on this grid; text and the prompt atlas do not. */
    float cell_w = (over - under) * 512.0f, cell_h = (bottom - top) * 256.0f;
    int cell = cell_w >= 1.0f && cell_h >= 1.0f && cell_w <= 65.0f && cell_h <= 65.0f &&
        fabsf(under * 16.0f - roundf(under * 16.0f)) < 0.02f &&
        fabsf(over * 16.0f - roundf(over * 16.0f)) < 0.02f &&
        fabsf(top * 8.0f - roundf(top * 8.0f)) < 0.02f &&
        fabsf(bottom * 8.0f - roundf(bottom * 8.0f)) < 0.02f;
    if (!middle && !dot && !cover && !cell) return;
    struct timespec clock_now;
    clock_gettime(CLOCK_MONOTONIC, &clock_now);
    double at = clock_now.tv_sec + clock_now.tv_nsec / 1e9;
    /* Still drawn every frame: says the trace's view is the live reticle. */
    static double last_seen;
    if (at - last_seen >= 5.0) {
        last_seen = at;
        fprintf(stderr, "[RETICLE] still drawn at +%.1fs\n", at - trace_start);
    }
    for (unsigned i = 0; i < count; ++i) {
        const TracedSprite *s = &seen[i];
        if (fabsf(s->u0 - under) < 0.002f && fabsf(s->v0 - top) < 0.002f &&
            fabsf(s->u1 - over) < 0.002f && fabsf(s->v1 - bottom) < 0.002f &&
            fabsf(s->w - w) < 0.5f && fabsf(s->h - h) < 0.5f)
            return;
    }
    if (count == sizeof seen / sizeof seen[0]) return;
    seen[count++] = (TracedSprite){under, top, over, bottom, w, h};
    fprintf(stderr,
            "[RETICLE] %s%s uv %.4f,%.4f %.4f,%.4f (atlas %.1f,%.1f %.1f,%.1f) "
            "at %.1f,%.1f %.1fx%.1f (+%.2fs)\n",
            kind, !middle && !dot ? (cell ? " (cell)" : " (cover)") : (cell ? " (cell)" : ""),
            under, top, over, bottom, under * 512.0f, top * 256.0f, over * 512.0f,
            bottom * 256.0f, cx, cy, w, h, at - trace_start);
}

void black_ReticleTraceQuads(uint32_t quads)
{
    if (!reticle_trace()) return;
    uint32_t uv_points = MEM32(g_esp + 0xC);
    if (reticle_trace()) {
        static int told;
        static uint32_t counted[16];
        static unsigned counts;
        if (!told) { told = 1; fprintf(stderr, "[RETICLE] quads: %u, uv %08x\n", quads, uv_points); }
        /* A batch past the traced limit would hide the HUD's icons: say once
         * how big the game's own batches get. */
        else if (quads > 256) {
            unsigned i = 0;
            while (i < counts && counted[i] != quads) ++i;
            if (i == counts && counts < sizeof counted / sizeof counted[0]) {
                counted[counts++] = quads;
                fprintf(stderr, "[RETICLE] big batch: %u quads\n", quads);
            }
        }
    }
    if (reticle_trace() && uv_points && quads && quads <= 256) {
        for (uint32_t q = 0; q < quads; ++q)
            reticle_trace_one(MEMF(g_eax + q * 16), MEMF(g_eax + q * 16 + 4),
                              MEMF(g_eax + q * 16 + 8), MEMF(g_eax + q * 16 + 12),
                              MEMF(uv_points + q * 16), MEMF(uv_points + q * 16 + 4),
                              MEMF(uv_points + q * 16 + 8), MEMF(uv_points + q * 16 + 12),
                              "quad");
    }
}

void black_ReticleTraceStrip(uint32_t points)
{
    if (!reticle_trace()) return;
    uint32_t uv_points = MEM32(g_esp + 0xC);
    if (reticle_trace()) {
        static int told;
        if (!told) { told = 1; fprintf(stderr, "[RETICLE] strips: %u, uv %08x\n", points, uv_points); }
    }
    if (reticle_trace() && uv_points && points >= 3 && points <= 128) {
        float x0 = MEMF(g_eax), x1 = x0, y0 = MEMF(g_eax + 4), y1 = y0;
        float u0 = MEMF(uv_points), u1 = u0, v0 = MEMF(uv_points + 4), v1 = v0;
        for (uint32_t i = 1; i < points; ++i) {
            float x = MEMF(g_eax + i * 8), y = MEMF(g_eax + i * 8 + 4);
            float u = MEMF(uv_points + i * 8), v = MEMF(uv_points + i * 8 + 4);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
            if (u < u0) u0 = u;
            if (u > u1) u1 = u;
            if (v < v0) v0 = v;
            if (v > v1) v1 = v;
        }
        reticle_trace_one(x0, y0, x1, y1, u0, v0, u1, v1, "strip");
    }
}

void black_ReticleTrace2DBegin(void)
{
    if (reticle_trace()) {
        static int told;
        if (!told) { told = 1; fprintf(stderr, "[RETICLE] 2D begin\n"); }
    }
}
