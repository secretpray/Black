/* The aiming dot's HUD atlas rebuilt from the guest's own texels (see
 * hud_reticle.h), at 4x with the HD setting and 2x without it. Format 0x0B is
 * 8-bit indices in the NV2A's swizzle, coloured through a BGRA palette; the top
 * level is decoded the way texture_dump.c does for its PNG, then upscaled with
 * alpha weighting, so edges gain no dark or bright fringes. Hard edges (rings,
 * crosses, chevrons, dashes, spread scales, soldier outlines) get their
 * coverage re-drawn with a one-pixel edge; soft ramps and glows stay on the
 * plain alpha upscale, with no step between. The dot itself (a white disc at
 * about 144, 176) is wiped and drawn anew with a one-pixel anti-aliased edge,
 * its centre and radius measured on the decoded atlas rather than assumed.
 * An empty 64x64 block gains the Reticle Shape mod's cross (reticle_shape.h),
 * drawn white with the same one-pixel edge over a dark rim, so the game's
 * tint colours it like the dot. Mips are box-filtered with alpha weighting:
 * the game draws the atlas smaller than this. */
#include "hud_reticle.h"
#include "reticle_shape.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f
#define PALETTE_FORMAT 0x0B
/* Where the aiming dot sits, in source pixels, from the dumped atlas. */
#define DOT_X 144.0f
#define DOT_Y 176.0f
/* The game's dot is 249 at full alpha with an anti-aliased rim (154, 29):
 * a silhouette radius of about 7.2 source pixels. */
#define DOT_RADIUS_MIN 6.0f
#define DOT_RADIUS_MAX 8.0f
/* An edge is redrawn at one output pixel when its core reaches CRISP_MIN_ALPHA
 * (fully from there, not at all below CRISP_CORE_FROM) and the alpha around it
 * drops by CRISP_STEP_FULL within one texel (not at all below CRISP_STEP_FROM).
 * The glow cells peak at about 96, and a glow's bright centre falls off over
 * several texels, so both keep the plain upscale. */
#define CRISP_MIN_ALPHA 200.0f
#define CRISP_CORE_FROM 150.0f
#define CRISP_STEP_FROM 100.0f
#define CRISP_STEP_FULL 180.0f
#define CRISP_SLOPE 3.0f

static float clampf(float v, float low, float high) { return v < low ? low : v > high ? high : v; }

int black_HudReticleWanted(uint64_t key)
{
    return key == BLACK_HUD_RETICLE_KEY;
}

/* The swizzled 8-bit atlas through its palette, as RGBA8. */
static uint8_t *decode(const uint8_t *data, const uint8_t *palette, unsigned w, unsigned h)
{
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return NULL;
    unsigned mask_x = 0, mask_y = 0, bit = 1, mask = 1;
    while (bit < w || bit < h) {
        if (bit < w) { mask_x |= mask; mask <<= 1; }
        if (bit < h) { mask_y |= mask; mask <<= 1; }
        bit <<= 1;
    }
    unsigned oy = 0;
    for (unsigned y = 0; y < h; ++y) {
        unsigned ox = 0;
        for (unsigned x = 0; x < w; ++x) {
            unsigned i = data[oy + ox];
            uint8_t *q = rgba + ((size_t)y * w + x) * 4;
            /* BGRA in guest memory. */
            q[0] = palette[i * 4 + 2];
            q[1] = palette[i * 4 + 1];
            q[2] = palette[i * 4];
            q[3] = palette[i * 4 + 3];
            ox = (ox - mask_x) & mask_x;
        }
        oy = (oy - mask_y) & mask_y;
    }
    return rgba;
}

/* How hard each source pixel's edge is, 0 to 1, from the alpha around it: an
 * opaque core next to a transparent texel (rings, crosses, chevrons, spread
 * scales, soldier outlines) is 1; a gradual ramp, such as a glow whose bright
 * centre fades over several texels, stays near 0 and keeps its own falloff. */
static float *edge_weights(const uint8_t *rgba, unsigned w, unsigned h)
{
    float *weight = malloc((size_t)w * h * sizeof *weight);
    if (!weight) return NULL;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            unsigned high = 0, low = 255;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int sx = (int)x + dx, sy = (int)y + dy;
                    unsigned a = sx < 0 || sy < 0 || sx >= (int)w || sy >= (int)h ? 0 :
                        rgba[((size_t)sy * w + sx) * 4 + 3];
                    if (a > high) high = a;
                    if (a < low) low = a;
                }
            float core = clampf(((float)high - CRISP_CORE_FROM) / (CRISP_MIN_ALPHA - CRISP_CORE_FROM), 0.0f, 1.0f);
            float step = clampf(((float)(high - low) - CRISP_STEP_FROM) / (CRISP_STEP_FULL - CRISP_STEP_FROM), 0.0f, 1.0f);
            weight[(size_t)y * w + x] = core * step;
        }
    return weight;
}

/* Bilinear at K times the size, each texel weighted by its alpha, so a texel
 * that is mostly transparent does not darken its neighbours. Where the edge
 * is hard its coverage is pulled toward a steep curve around the half level,
 * which turns the one-source-pixel rim into one output pixel. The pull is
 * interpolated like the alpha, so a hard edge and a soft ramp meet without a
 * step, and a glow is left as it is. */
static void upscale(const uint8_t *src, unsigned sw, unsigned sh, const float *hard,
                    uint8_t *dst, unsigned dw, unsigned dh, unsigned k)
{
    for (unsigned y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) / k - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        for (unsigned x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) / k - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            int ix[4] = {x0, x0 + 1, x0, x0 + 1}, iy[4] = {y0, y0, y0 + 1, y0 + 1};
            float weight[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
            float alpha = 0, r = 0, g = 0, b = 0, pull = 0;
            for (int i = 0; i < 4; ++i) {
                int sx = ix[i] < 0 ? 0 : ix[i] >= (int)sw ? (int)sw - 1 : ix[i];
                int sy = iy[i] < 0 ? 0 : iy[i] >= (int)sh ? (int)sh - 1 : iy[i];
                const uint8_t *p = src + ((size_t)sy * sw + sx) * 4;
                float a = p[3] * weight[i];
                alpha += a;
                r += p[0] * a;
                g += p[1] * a;
                b += p[2] * a;
                if (hard) pull += hard[(size_t)sy * sw + sx] * weight[i];
            }
            uint8_t *q = dst + ((size_t)y * dw + x) * 4;
            if (alpha < 0.5f) {
                q[0] = q[1] = q[2] = q[3] = 0;
                continue;
            }
            float steep = clampf((alpha - 127.5f) * CRISP_SLOPE + 127.5f, 0.0f, 255.0f);
            float coverage = alpha + (steep - alpha) * pull;
            if (coverage < 0.5f) {
                q[0] = q[1] = q[2] = q[3] = 0;
            } else {
                q[0] = (uint8_t)(r / alpha + 0.5f);
                q[1] = (uint8_t)(g / alpha + 0.5f);
                q[2] = (uint8_t)(b / alpha + 0.5f);
                q[3] = (uint8_t)(coverage + 0.5f);
            }
        }
    }
}

/* Wipes the original dot and draws a white one at `radius`, one destination
 * pixel of anti-aliasing; the game's tint still colours it. */
static void draw_dot(uint8_t *dst, unsigned dw, unsigned dh, float cx, float cy, float radius, unsigned k)
{
    float centre_x = cx * k, centre_y = cy * k, edge = radius * k;
    float clear = edge + 0.75f * k;
    int x0 = (int)floorf(centre_x - clear), x1 = (int)ceilf(centre_x + clear);
    int y0 = (int)floorf(centre_y - clear), y1 = (int)ceilf(centre_y + clear);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)dw) x1 = (int)dw;
    if (y1 > (int)dh) y1 = (int)dh;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - centre_x, dy = y + 0.5f - centre_y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > clear) continue;
            uint8_t *q = dst + ((size_t)y * dw + x) * 4;
            q[0] = q[1] = q[2] = 255;
            q[3] = (uint8_t)(clampf(edge + 0.5f - d, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
}

/* Distance from a point to an axis-aligned segment's square-ended span (its
 * segment with no round caps), in the same units: 0 on the line, the
 * distance past an end along the line otherwise. */
static float box_distance(float px, float py, const float e[4])
{
    float lo_x = fminf(e[0], e[2]), hi_x = fmaxf(e[0], e[2]);
    float lo_y = fminf(e[1], e[3]), hi_y = fmaxf(e[1], e[3]);
    float dx = px < lo_x ? lo_x - px : px > hi_x ? px - hi_x : 0.0f;
    float dy = py < lo_y ? lo_y - py : py > hi_y ? py - hi_y : 0.0f;
    return fmaxf(dx, dy);
}

/* The painted cross (reticle_shape.h), in its cell's own 64x64 texels, drawn
 * in the dot's quad: one texel is about 0.035% of the screen's height. Four
 * white bars run from 8.5 to 24.5 texels out (an open centre about 0.6% of the
 * height across, the bars about 0.55% long, 1.7% across in all), 5.5 texels
 * (about 0.19%, two pixels at 1080p) thick; each segment is inset by the
 * half-thickness, as a bar's square end reaches that far past it. A black rim
 * of 2.5 texels (about one pixel at 1080p) at 60% surrounds them: the game's
 * tint leaves it black, so the white mark reads on bright walls and sky. */
enum { CROSS_BARS = 4 };
static const float cross_bars[CROSS_BARS][4] = {
    {20.75f, 32, 10.25f, 32}, {43.25f, 32, 53.75f, 32}, {32, 20.75f, 32, 10.25f}, {32, 43.25f, 32, 53.75f},
};
#define CROSS_HALF 2.75f
#define CROSS_RIM 2.5f
#define CROSS_RIM_ALPHA 0.6f

/* The cross's cell: white bars with a one destination pixel anti-aliased
 * edge, like the dot, over the black rim; the block is empty, so nothing of
 * the game's is overwritten. */
static void draw_cross(uint8_t *dst, unsigned dw, unsigned dh, unsigned k)
{
    const BlackReticleShape *cell = &black_reticle_shapes[BLACK_RETICLE_CROSS];
    unsigned x0 = (unsigned)(cell->x0 * k), x1 = (unsigned)(cell->x1 * k);
    unsigned y0 = (unsigned)(cell->y0 * k), y1 = (unsigned)(cell->y1 * k);
    for (unsigned y = y0; y < y1 && y < dh; ++y)
        for (unsigned x = x0; x < x1 && x < dw; ++x) {
            float sx = (x + 0.5f) / k - cell->x0, sy = (y + 0.5f) / k - cell->y0;
            float d = 1e9f;
            for (int i = 0; i < CROSS_BARS; ++i) d = fminf(d, box_distance(sx, sy, cross_bars[i]));
            float core = clampf(CROSS_HALF * k + 0.5f - d * k, 0.0f, 1.0f);
            float rim = CROSS_RIM_ALPHA * clampf((CROSS_HALF + CROSS_RIM) * k + 0.5f - d * k, 0.0f, 1.0f);
            float a = core + rim * (1.0f - core);
            if (a <= 0.0f) continue;
            uint8_t *q = dst + ((size_t)y * dw + x) * 4;
            q[0] = q[1] = q[2] = (uint8_t)(core / a * 255.0f + 0.5f);
            q[3] = (uint8_t)(a * 255.0f + 0.5f);
        }
}

/* The next level: 2x2 boxes averaged with alpha weighting. */
static void box_down(const uint8_t *src, unsigned sw, unsigned sh, uint8_t *dst, unsigned dw, unsigned dh)
{
    for (unsigned y = 0; y < dh; ++y)
        for (unsigned x = 0; x < dw; ++x) {
            unsigned alpha = 0, r = 0, g = 0, b = 0;
            for (unsigned dy = 0; dy < 2; ++dy)
                for (unsigned dx = 0; dx < 2; ++dx) {
                    unsigned sx = 2 * x + dx, sy = 2 * y + dy;
                    if (sx >= sw) sx = sw - 1;
                    if (sy >= sh) sy = sh - 1;
                    const uint8_t *p = src + ((size_t)sy * sw + sx) * 4;
                    alpha += p[3];
                    r += p[0] * p[3];
                    g += p[1] * p[3];
                    b += p[2] * p[3];
                }
            uint8_t *q = dst + ((size_t)y * dw + x) * 4;
            if (!alpha) {
                q[0] = q[1] = q[2] = q[3] = 0;
            } else {
                q[0] = (uint8_t)(r / alpha);
                q[1] = (uint8_t)(g / alpha);
                q[2] = (uint8_t)(b / alpha);
                q[3] = (uint8_t)((alpha + 2) / 4);
            }
        }
}

int black_HudReticleBuild(unsigned width, unsigned height, int color_format,
                          const uint8_t *data, size_t length,
                          const uint8_t *palette, size_t palette_length,
                          unsigned scale, BlackHudReticleImage *image)
{
    if (image) memset(image, 0, sizeof *image);
    if (!image || color_format != PALETTE_FORMAT || width != BLACK_HUD_RETICLE_W ||
        height != BLACK_HUD_RETICLE_H || !data || !palette ||
        (scale != BLACK_HUD_RETICLE_SCALE && scale != BLACK_HUD_RETICLE_PLAIN_SCALE))
        return 0;
    size_t texels = (size_t)width * height;
    if (length < texels || palette_length < 128 || palette_length > 1024 || palette_length % 8)
        return 0;
    for (size_t i = 0; i < texels; ++i)
        if (data[i] * 4u + 4u > palette_length) return 0;
    uint8_t *src = decode(data, palette, width, height);
    if (!src) return 0;
    float *hard = edge_weights(src, width, height);

    /* The aiming dot: what is left of the white disc near the atlas centre. */
    double mass = 0, sum_x = 0, sum_y = 0;
    unsigned count = 0;
    for (unsigned y = 160; y < 193; ++y)
        for (unsigned x = 130; x < 161; ++x) {
            unsigned a = src[((size_t)y * width + x) * 4 + 3];
            if (a < 128) continue;
            mass += a;
            sum_x += (x + 0.5) * a;
            sum_y += (y + 0.5) * a;
            count++;
        }
    float cx = 0, cy = 0, radius = 0;
    if (count >= 100 && mass > 0) {
        cx = (float)(sum_x / mass);
        cy = (float)(sum_y / mass);
        radius = sqrtf((float)count / PI);
    }
    const uint8_t *centre = src + ((size_t)175 * width + 143) * 4;
    if (count < 100 || count > 260 || fabsf(cx - DOT_X) > 2.0f || fabsf(cy - DOT_Y) > 2.0f ||
        radius < DOT_RADIUS_MIN || radius > DOT_RADIUS_MAX ||
        centre[3] < 200 || centre[0] < 200 || centre[1] < 200 || centre[2] < 200) {
        free(hard);
        free(src);
        return 0;
    }
    /* The area of the opaque pixels sits inside the anti-aliased rim. */
    radius += 0.15f;

    /* The painted cells go into blocks the game leaves empty; a block with
     * anything in it belongs to some other sprite, so the rebuild refuses. */
    for (int shape = 0; shape < BLACK_RETICLE_SHAPES; ++shape) {
        if (!black_reticle_painted(shape)) continue;
        const BlackReticleShape *cell = &black_reticle_shapes[shape];
        if (cell->x1 - cell->x0 != 64 || cell->y1 - cell->y0 != 64) {
            free(hard);
            free(src);
            return 0;
        }
        for (unsigned y = (unsigned)cell->y0; y < (unsigned)cell->y1; ++y)
            for (unsigned x = (unsigned)cell->x0; x < (unsigned)cell->x1; ++x)
                if (src[((size_t)y * width + x) * 4 + 3]) {
                    free(hard);
                    free(src);
                    return 0;
                }
    }

    unsigned dw = width * scale, dh = height * scale;
    unsigned levels = 1;
    for (unsigned side = dw > dh ? dw : dh; side > 1; side >>= 1) levels++;
    if (levels > BLACK_HUD_RETICLE_LEVELS) {
        free(hard);
        free(src);
        return 0;
    }
    size_t total = 0;
    for (unsigned level = 0, lw = dw, lh = dh; level < levels; ++level) {
        total += (size_t)lw * lh * 4;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    uint8_t *storage = malloc(total);
    if (!storage) {
        free(hard);
        free(src);
        return 0;
    }
    size_t offset = 0;
    for (unsigned level = 0, lw = dw, lh = dh; level < levels; ++level) {
        image->level[level] = storage + offset;
        image->level_size[level] = (size_t)lw * lh * 4;
        offset += image->level_size[level];
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    upscale(src, width, height, hard, image->level[0], dw, dh, scale);
    draw_dot(image->level[0], dw, dh, cx, cy, radius, scale);
    for (int shape = 0; shape < BLACK_RETICLE_SHAPES; ++shape)
        if (black_reticle_painted(shape)) draw_cross(image->level[0], dw, dh, scale);
    for (unsigned level = 1, lw = dw, lh = dh; level < levels; ++level) {
        unsigned nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
        box_down(image->level[level - 1], lw, lh, image->level[level], nw, nh);
        lw = nw;
        lh = nh;
    }
    image->width = dw;
    image->height = dh;
    image->levels = levels;
    image->dot_x = cx;
    image->dot_y = cy;
    image->dot_radius = radius;
    image->storage = storage;
    free(hard);
    free(src);
    return 1;
}

void black_HudReticleFree(BlackHudReticleImage *image)
{
    if (!image) return;
    free(image->storage);
    memset(image, 0, sizeof *image);
}
