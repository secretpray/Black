/* The HD reticle's atlas rebuild (hud_reticle.h) on a made-up 512x256
 * paletted atlas: the size, the dot's measured place and edge, the crisp
 * rim of an opaque cell, the untouched glow, the painted cross, chevron and
 * crosshair cells, the mips, and the refusals. No game data is used or needed. */
#undef NDEBUG
#include "../../ports/macos/overrides/hud_reticle.h"
#include "../../ports/macos/overrides/reticle_shape.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 512, H = 256, PALETTE_BYTES = 1024 };
/* BGRA palette entries: 0 unused, 1 the aiming dot, 2 a bright mark, 3 the
 * transparent background, 4 a soft glow. */
static const uint8_t palette[PALETTE_BYTES] = {
    [0 * 4 + 0] = 0,   [0 * 4 + 1] = 0,   [0 * 4 + 2] = 0,   [0 * 4 + 3] = 0,
    [1 * 4 + 0] = 255, [1 * 4 + 1] = 255, [1 * 4 + 2] = 255, [1 * 4 + 3] = 249,
    [2 * 4 + 0] = 20,  [2 * 4 + 1] = 120, [2 * 4 + 2] = 200, [2 * 4 + 3] = 255,
    [3 * 4 + 0] = 10,  [3 * 4 + 1] = 10,  [3 * 4 + 2] = 10,  [3 * 4 + 3] = 64,
    [4 * 4 + 0] = 255, [4 * 4 + 1] = 255, [4 * 4 + 2] = 255, [4 * 4 + 3] = 96,
};

/* The same entries and a glow whose centre is opaque: 9 at the middle, then
 * 8, 7, 6, 5 a texel further out each. */
static uint8_t glow_palette[PALETTE_BYTES];
static void make_glow_palette(void)
{
    static const uint8_t alpha[5] = {80, 120, 160, 200, 240};
    memcpy(glow_palette, palette, sizeof glow_palette);
    for (int i = 0; i < 5; ++i) {
        glow_palette[(5 + i) * 4 + 0] = glow_palette[(5 + i) * 4 + 1] = glow_palette[(5 + i) * 4 + 2] = 255;
        glow_palette[(5 + i) * 4 + 3] = alpha[i];
    }
}

/* Writes 8-bit indices into the NV2A's swizzle, the way texture_dump.c reads. */
static void encode(const uint8_t *linear, uint8_t *data)
{
    unsigned mask_x = 0, mask_y = 0, bit = 1, mask = 1;
    while (bit < W || bit < H) {
        if (bit < W) { mask_x |= mask; mask <<= 1; }
        if (bit < H) { mask_y |= mask; mask <<= 1; }
        bit <<= 1;
    }
    unsigned oy = 0;
    for (unsigned y = 0; y < H; ++y) {
        unsigned ox = 0;
        for (unsigned x = 0; x < W; ++x) {
            data[oy + ox] = linear[y * W + x];
            ox = (ox - mask_x) & mask_x;
        }
        oy = (oy - mask_y) & mask_y;
    }
}

static void make_atlas(uint8_t *linear, const float *disc)
{
    memset(linear, 3, W * H);
    /* The two blocks the painted reticle cells go into are empty, as the
     * game's atlas leaves them. */
    for (int shape = 0; shape < BLACK_RETICLE_SHAPES; ++shape) {
        if (!black_reticle_painted(shape)) continue;
        const BlackReticleShape *cell = &black_reticle_shapes[shape];
        for (unsigned y = (unsigned)cell->y0; y < (unsigned)cell->y1; ++y)
            for (unsigned x = (unsigned)cell->x0; x < (unsigned)cell->x1; ++x)
                linear[y * W + x] = 0;
    }
    if (disc) {
        for (unsigned y = 0; y < H; ++y)
            for (unsigned x = 0; x < W; ++x) {
                float dx = x + 0.5f - disc[0], dy = y + 0.5f - disc[1];
                if (dx * dx + dy * dy <= disc[2] * disc[2]) linear[y * W + x] = 1;
            }
    }
    /* An 8x8 mark away from the dot, to see the upscale. */
    for (unsigned y = 100; y < 108; ++y)
        for (unsigned x = 300; x < 308; ++x) linear[y * W + x] = 2;
    /* A soft glow cell: peak alpha 96, which keeps the plain upscale. */
    for (unsigned y = 100; y < 108; ++y)
        for (unsigned x = 400; x < 408; ++x) linear[y * W + x] = 4;
}

static unsigned alpha_at(const BlackHudReticleImage *image, unsigned x, unsigned y)
{
    return image->level[0][((size_t)y * image->width + x) * 4 + 3];
}

int main(void)
{
    static uint8_t linear[W * H], data[W * H];
    make_glow_palette();
    const float disc[3] = {144.0f, 176.0f, 7.0f};
    make_atlas(linear, disc);
    encode(linear, data);

    assert(black_HudReticleWanted(BLACK_HUD_RETICLE_KEY));
    assert(!black_HudReticleWanted(BLACK_HUD_RETICLE_KEY ^ 1));

    BlackHudReticleImage image;
    assert(black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette,
                                 BLACK_HUD_RETICLE_SCALE, &image));
    assert(image.width == W * BLACK_HUD_RETICLE_SCALE && image.height == H * BLACK_HUD_RETICLE_SCALE);
    assert(image.levels == 12 && image.level[11] && image.level_size[11] == 4);
    assert(fabsf(image.dot_x - 144.0f) < 0.5f && fabsf(image.dot_y - 176.0f) < 0.5f);
    assert(image.dot_radius > 6.5f && image.dot_radius < 7.5f);

    /* The dot is white and opaque at its centre, and the old rim is gone. */
    assert(alpha_at(&image, 144 * 4, 176 * 4) == 255);
    unsigned centre = ((size_t)(176 * 4) * image.width + 144 * 4) * 4;
    assert(image.level[0][centre] == 255 && image.level[0][centre + 1] == 255 && image.level[0][centre + 2] == 255);
    assert(alpha_at(&image, 144 * 4 + 30, 176 * 4) == 0);
    /* The background and the far mark survive the upscale. */
    assert(alpha_at(&image, 100 * 4, 60 * 4) == 64);
    assert(alpha_at(&image, 301 * 4 + 2, 101 * 4 + 2) == 255);
    /* The opaque mark's rim is re-drawn within about one output pixel and
     * keeps the mark's colour; the soft glow keeps its gradual ramp. Outside
     * the rim the background eases down to it, with no step between pixels. */
    assert(alpha_at(&image, 1192, 101 * 4 + 2) == 64);
    assert(alpha_at(&image, 1198, 101 * 4 + 2) <= 16);
    assert(alpha_at(&image, 1199, 101 * 4 + 2) > 128 && alpha_at(&image, 1199, 101 * 4 + 2) < 200);
    assert(alpha_at(&image, 1200, 101 * 4 + 2) == 255);
    for (unsigned x = 1192; x < 1197; ++x) {
        unsigned a = alpha_at(&image, x, 101 * 4 + 2), b = alpha_at(&image, x + 1, 101 * 4 + 2);
        assert(b <= a && a - b <= 16);
    }
    /* The colour stays the plain alpha-weighted blend (144/88/17 of the mark
     * and the 64-alpha background), not scaled by the curved coverage. */
    unsigned rim = ((size_t)(101 * 4 + 2) * image.width + 1199) * 4;
    assert(image.level[0][rim] == 144 && image.level[0][rim + 1] == 88 && image.level[0][rim + 2] == 17);
    assert(alpha_at(&image, 404 * 4 + 2, 104 * 4 + 2) == 96);
    assert(alpha_at(&image, 399 * 4 + 2, 104 * 4 + 2) == 68);
    /* The painted cross: opaque along both bars, transparent off them and in
     * the block's corners, white so the game's tint colours it. */
    assert(alpha_at(&image, 352 * 4 + 2, 32 * 4 + 2) == 255);
    assert(alpha_at(&image, 340 * 4 + 2, 32 * 4 + 2) == 255);
    assert(alpha_at(&image, 352 * 4 + 2, 20 * 4 + 2) == 255);
    assert(alpha_at(&image, 328 * 4 + 2, 32 * 4 + 2) == 0);
    assert(alpha_at(&image, 352 * 4 + 2, 8 * 4 + 2) == 0);
    assert(alpha_at(&image, 322 * 4 + 2, 2 * 4 + 2) == 0);
    assert(alpha_at(&image, 360 * 4 + 2, 40 * 4 + 2) == 0);
    unsigned cross = ((size_t)(32 * 4 + 2) * image.width + 352 * 4 + 2) * 4;
    assert(image.level[0][cross] == 255 && image.level[0][cross + 1] == 255 && image.level[0][cross + 2] == 255);
    /* The painted chevron: an apex at the cell's centre and two legs down and
     * out; nothing between the legs or above the apex. */
    assert(alpha_at(&image, 352 * 4 + 2, 160 * 4 + 2) == 255);
    assert(alpha_at(&image, 340 * 4 + 2, 172 * 4 + 2) == 255);
    assert(alpha_at(&image, 364 * 4 + 2, 172 * 4 + 2) == 255);
    assert(alpha_at(&image, 352 * 4 + 2, 184 * 4 + 2) == 0);
    assert(alpha_at(&image, 352 * 4 + 2, 140 * 4 + 2) == 0);
    /* The open crosshair: four white bars 12..26 texels from the centre, an
     * empty centre, and a translucent black rim beside each bar. */
    assert(alpha_at(&image, 352 * 4 + 2, 224 * 4 + 2) == 0);
    assert(alpha_at(&image, 360 * 4 + 2, 224 * 4 + 2) == 0);
    assert(alpha_at(&image, 370 * 4 + 2, 224 * 4 + 2) == 255);
    assert(alpha_at(&image, 334 * 4 + 2, 224 * 4 + 2) == 255);
    assert(alpha_at(&image, 352 * 4 + 2, 206 * 4 + 2) == 255);
    assert(alpha_at(&image, 352 * 4 + 2, 242 * 4 + 2) == 255);
    assert(alpha_at(&image, 382 * 4 + 2, 224 * 4 + 2) == 0);
    assert(alpha_at(&image, 340 * 4 + 2, 212 * 4 + 2) == 0);
    unsigned bar = ((size_t)(224 * 4 + 2) * image.width + 370 * 4 + 2) * 4;
    assert(image.level[0][bar] == 255 && image.level[0][bar + 1] == 255 && image.level[0][bar + 2] == 255);
    unsigned edge = ((size_t)903 * image.width + 370 * 4 + 2) * 4;
    assert(image.level[0][edge + 3] == 140 && image.level[0][edge] == 0 && image.level[0][edge + 2] == 0);
    /* The same picture at the plain scale, for Original with a painted shape. */
    BlackHudReticleImage plain;
    assert(black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette,
                                 BLACK_HUD_RETICLE_PLAIN_SCALE, &plain));
    assert(plain.width == W * BLACK_HUD_RETICLE_PLAIN_SCALE);
    assert(alpha_at(&plain, 352 * 2 + 1, 32 * 2 + 1) == 255);
    assert(alpha_at(&plain, 352 * 2 + 1, 160 * 2 + 1) == 255);
    assert(alpha_at(&plain, 322 * 2 + 1, 2 * 2 + 1) == 0);
    assert(alpha_at(&plain, 370 * 2 + 1, 224 * 2 + 1) == 255);
    assert(alpha_at(&plain, 352 * 2 + 1, 224 * 2 + 1) == 0);
    black_HudReticleFree(&plain);
    assert(!black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette, 3, &plain));
    /* The 2x mip keeps the rim: half coverage at the edge, full inside. */
    size_t mip = ((size_t)(406 / 2) * (image.width / 2) + 599) * 4;
    assert(image.level[1][mip + 3] > 40 && image.level[1][mip + 3] < 128);
    mip = ((size_t)(406 / 2) * (image.width / 2) + 600) * 4;
    assert(image.level[1][mip + 3] == 255);
    /* A glow with a bright centre that fades over several texels keeps its
     * ramp: no hard plateau where its core crosses the opaque level. */
    make_atlas(linear, disc);
    {
        static const uint8_t ramp[5] = {9, 8, 7, 6, 5};
        for (unsigned y = 40; y < 60; ++y)
            for (unsigned x = 40; x < 60; ++x) {
                unsigned d = (unsigned)(abs((int)x - 50) > abs((int)y - 50) ? abs((int)x - 50) : abs((int)y - 50));
                if (d < 5) linear[y * W + x] = ramp[d];
            }
    }
    encode(linear, data);
    BlackHudReticleImage glow;
    assert(black_HudReticleBuild(W, H, 0x0B, data, sizeof data, glow_palette, sizeof glow_palette,
                                 BLACK_HUD_RETICLE_SCALE, &glow));
    /* From the glow's outer texel to its centre: rising by a texel's step
     * spread over four pixels, never a jump. */
    for (unsigned x = 47 * 4; x < 50 * 4; ++x) {
        unsigned a = alpha_at(&glow, x, 50 * 4 + 2), b = alpha_at(&glow, x + 1, 50 * 4 + 2);
        assert(b >= a && b - a <= 12);
    }
    black_HudReticleFree(&glow);
    make_atlas(linear, disc);
    encode(linear, data);

    /* A full alpha-weighted chain. */
    assert(image.level[11][3] > 0 && image.level[11][3] <= 255);

    black_HudReticleFree(&image);
    assert(!image.storage && !image.level[0] && !image.levels);

    /* Not the atlas: no disc, another size or format, an index off the
     * palette, or a painted block the game did not leave empty. */
    make_atlas(linear, NULL);
    encode(linear, data);
    assert(!black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette,
                                  BLACK_HUD_RETICLE_SCALE, &image));
    make_atlas(linear, disc);
    encode(linear, data);
    assert(!black_HudReticleBuild(W / 2, H / 2, 0x0B, data, sizeof data, palette, sizeof palette,
                                  BLACK_HUD_RETICLE_SCALE, &image));
    assert(!black_HudReticleBuild(W, H, 0x0C, data, sizeof data, palette, sizeof palette,
                                  BLACK_HUD_RETICLE_SCALE, &image));
    data[0] = 250;
    assert(!black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, 128,
                                  BLACK_HUD_RETICLE_SCALE, &image));
    data[0] = 3;
    linear[32 * W + 352] = 2;
    encode(linear, data);
    assert(!black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette,
                                  BLACK_HUD_RETICLE_SCALE, &image));

    puts("HD reticle atlas: 4x and 2x upscale, crisp hard rims, soft glows and ramps without a step, redrawn dot, painted cross, chevron and crosshair, mips and refusals: PASS");
    return 0;
}
