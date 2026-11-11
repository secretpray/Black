/* The HD reticle's atlas rebuild (hud_reticle.h) on a made-up 512x256
 * paletted atlas: the size, the dot's measured place and edge, the crisp
 * rim of an opaque cell, the untouched glow, the mips, and the refusals. No
 * game data is used or needed. */
#undef NDEBUG
#include "../../ports/macos/overrides/hud_reticle.h"
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
    const float disc[3] = {144.0f, 176.0f, 7.0f};
    make_atlas(linear, disc);
    encode(linear, data);

    assert(black_HudReticleWanted(BLACK_HUD_RETICLE_KEY));
    assert(!black_HudReticleWanted(BLACK_HUD_RETICLE_KEY ^ 1));

    BlackHudReticleImage image;
    assert(black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette, &image));
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
     * keeps the mark's colour; the soft glow keeps its gradual ramp. */
    assert(alpha_at(&image, 1196, 101 * 4 + 2) == 0);
    assert(alpha_at(&image, 1198, 101 * 4 + 2) <= 16);
    assert(alpha_at(&image, 1200, 101 * 4 + 2) == 255);
    /* The colour stays the plain alpha-weighted blend (144/88/17 of the mark
     * and the 64-alpha background), not scaled by the curved coverage. */
    unsigned rim = ((size_t)(101 * 4 + 2) * image.width + 1199) * 4;
    assert(image.level[0][rim] == 144 && image.level[0][rim + 1] == 88 && image.level[0][rim + 2] == 17);
    assert(alpha_at(&image, 404 * 4 + 2, 104 * 4 + 2) == 96);
    assert(alpha_at(&image, 399 * 4 + 2, 104 * 4 + 2) == 68);
    /* The 2x mip keeps the rim: half coverage at the edge, full inside. */
    size_t mip = ((size_t)(406 / 2) * (image.width / 2) + 599) * 4;
    assert(image.level[1][mip + 3] > 40 && image.level[1][mip + 3] < 128);
    mip = ((size_t)(406 / 2) * (image.width / 2) + 600) * 4;
    assert(image.level[1][mip + 3] == 255);
    /* A full alpha-weighted chain. */
    assert(image.level[11][3] > 0 && image.level[11][3] <= 255);

    black_HudReticleFree(&image);
    assert(!image.storage && !image.level[0] && !image.levels);

    /* Not the atlas: no disc, another size or format, or an index off the palette. */
    make_atlas(linear, NULL);
    encode(linear, data);
    assert(!black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, sizeof palette, &image));
    make_atlas(linear, disc);
    encode(linear, data);
    assert(!black_HudReticleBuild(W / 2, H / 2, 0x0B, data, sizeof data, palette, sizeof palette, &image));
    assert(!black_HudReticleBuild(W, H, 0x0C, data, sizeof data, palette, sizeof palette, &image));
    data[0] = 250;
    assert(!black_HudReticleBuild(W, H, 0x0B, data, sizeof data, palette, 128, &image));

    puts("HD reticle atlas: 4x upscale, crisp opaque rims, soft glows, redrawn dot, mips and refusals: PASS");
    return 0;
}
