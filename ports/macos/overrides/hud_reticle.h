#ifndef BLACK_HUD_RETICLE_INCLUDED
#define BLACK_HUD_RETICLE_INCLUDED
/* The HUD atlas holding BLACK's aiming dot, rebuilt at 4x from the game's own
 * texels: an alpha-weighted upscale, an opaque cell's rim re-drawn at one output
 * pixel, the dot drawn anew, alpha-aware mips. No picture ships with the port;
 * the guest's 512x256 paletted atlas is the only input, and the game still tints
 * the dot through its own tables. */
#include <stddef.h>
#include <stdint.h>

/* The atlas is 512x256, 8-bit paletted (NV2A colour format 0x0B). */
#define BLACK_HUD_RETICLE_KEY 0xad6f441d17b7de95ull
enum { BLACK_HUD_RETICLE_W = 512, BLACK_HUD_RETICLE_H = 256, BLACK_HUD_RETICLE_SCALE = 4,
       BLACK_HUD_RETICLE_LEVELS = 16 };

/* RGBA8 levels, largest first, inside one allocation. `dot` is the aiming
 * disc's centre and silhouette radius in source pixels, as measured. */
typedef struct {
    unsigned width, height, levels;
    uint8_t *level[BLACK_HUD_RETICLE_LEVELS];
    size_t level_size[BLACK_HUD_RETICLE_LEVELS];
    float dot_x, dot_y, dot_radius;
    void *storage;
} BlackHudReticleImage;

/* 1 for the atlas this knows how to rebuild. */
int black_HudReticleWanted(uint64_t key);

/* Builds the 4x picture: 0 when the top level is not the expected atlas (its
 * size, palette or aiming dot differ), so the game's texture stays. */
int black_HudReticleBuild(unsigned width, unsigned height, int color_format,
                          const uint8_t *data, size_t length,
                          const uint8_t *palette, size_t palette_length,
                          BlackHudReticleImage *image);
void black_HudReticleFree(BlackHudReticleImage *image);
#endif /* BLACK_HUD_RETICLE_INCLUDED */
