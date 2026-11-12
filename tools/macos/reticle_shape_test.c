/* Reticle Shape (reticle_shape.h): every shape puts its atlas cell on the
 * aiming dot's quad and collapses the glow's; two of the cells are painted
 * into empty atlas blocks by the texture rebuild (hud_reticle.c). Nothing else
 * on the HUD changes. No game data is used. */
#undef NDEBUG
#include "../../ports/macos/overrides/reticle_shape.h"
#include "../../ports/macos/overrides/game_mods.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void)
{
    /* The dot the game draws: 8x10.7 HUD pixels at the centre, uv of atlas
     * 128..160 x 160..192. */
    const float dot_xy[4] = {316.0f, 234.65f, 324.0f, 245.35f};
    const float dot_uv[4] = {0.25f, 0.625f, 0.3125f, 0.75f};
    const float glow_uv[4] = {0.3125f, 0.625f, 0.375f, 0.75f};
    float xy[4], uv[4];

    /* The default keeps the game's dot exactly. */
    memcpy(xy, dot_xy, sizeof xy); memcpy(uv, dot_uv, sizeof uv);
    assert(!black_reticle_reshape(xy, uv, BLACK_RETICLE_DOT));
    assert(!memcmp(xy, dot_xy, sizeof xy) && !memcmp(uv, dot_uv, sizeof uv));
    assert(!black_reticle_reshape(xy, uv, BLACK_RETICLE_SHAPES) && !black_reticle_reshape(xy, uv, -1));

    /* Each shape: its own cell, grown around the same centre. */
    for (int shape = BLACK_RETICLE_RING; shape < BLACK_RETICLE_SHAPES; ++shape) {
        const BlackReticleShape *s = &black_reticle_shapes[shape];
        memcpy(xy, dot_xy, sizeof xy); memcpy(uv, dot_uv, sizeof uv);
        assert(black_reticle_reshape(xy, uv, shape));
        assert(near(uv[0], s->x0 / 512.0f) && near(uv[1], s->y0 / 256.0f));
        assert(near(uv[2], s->x1 / 512.0f) && near(uv[3], s->y1 / 256.0f));
        assert(near((xy[0] + xy[2]) * 0.5f, 320.0f) && near((xy[1] + xy[3]) * 0.5f, 240.0f));
        assert(near(xy[2] - xy[0], 8.0f * s->grow) && near(xy[3] - xy[1], 10.7f * s->grow));
    }
    /* The game's own cells, as the dumped atlas has them. */
    assert(black_reticle_shapes[BLACK_RETICLE_RING].x1 == 64 && black_reticle_shapes[BLACK_RETICLE_RING].y1 == 64);
    assert(black_reticle_shapes[BLACK_RETICLE_TICKS].y0 == 64);
    assert(black_reticle_shapes[BLACK_RETICLE_CHEVRONS].x0 == 192);
    /* The painted cell: the cross alone, in an empty 64x64 block away from
     * every icon, drawn in the dot's own quad, so at the dot's size. */
    assert(black_reticle_painted(BLACK_RETICLE_CROSS));
    assert(!black_reticle_painted(BLACK_RETICLE_RING) && !black_reticle_painted(BLACK_RETICLE_DOT) &&
           !black_reticle_painted(BLACK_RETICLE_CHEVRONS));
    assert(black_reticle_shapes[BLACK_RETICLE_CROSS].x0 == 320 && black_reticle_shapes[BLACK_RETICLE_CROSS].y0 == 0);
    assert(black_reticle_shapes[BLACK_RETICLE_CROSS].grow == 1.0f);
    for (int shape = BLACK_RETICLE_RING; shape < BLACK_RETICLE_SHAPES; ++shape) {
        const BlackReticleShape *s = &black_reticle_shapes[shape];
        assert(s->x1 - s->x0 == 64 && s->y1 - s->y0 == 64);
    }

    /* A flipped quad keeps its corner order on both axes. */
    float flip_xy[4] = {324.0f, 245.35f, 316.0f, 234.65f}, flip_uv[4] = {0.3125f, 0.75f, 0.25f, 0.625f};
    assert(black_reticle_reshape(flip_xy, flip_uv, BLACK_RETICLE_RING));
    assert(flip_xy[0] > flip_xy[2] && flip_xy[1] > flip_xy[3] && flip_uv[0] > flip_uv[2] && flip_uv[1] > flip_uv[3]);
    assert(near(flip_uv[0], 64.0f / 512.0f) && near(flip_uv[2], 0.0f));
    float corner_xy[4] = {324.0f, 245.35f, 316.0f, 234.65f}, corner_uv[4] = {0.3125f, 0.75f, 0.25f, 0.625f};
    assert(black_reticle_reshape(corner_xy, corner_uv, BLACK_RETICLE_CROSS));
    assert(corner_xy[0] > corner_xy[2] && corner_xy[1] > corner_xy[3]);
    assert(corner_uv[0] > corner_uv[2] && corner_uv[1] > corner_uv[3]);
    assert(near(corner_uv[0], 384.0f / 512.0f) && near(corner_uv[2], 320.0f / 512.0f));

    /* The glow quad collapses to its centre for every shape, so it draws
     * nothing and the mark is drawn once from the dot's quad. */
    for (int shape = BLACK_RETICLE_RING; shape < BLACK_RETICLE_SHAPES; ++shape) {
        memcpy(xy, dot_xy, sizeof xy); memcpy(uv, glow_uv, sizeof uv);
        assert(black_reticle_reshape(xy, uv, shape) == 1);
        assert(xy[0] == xy[2] && xy[1] == xy[3] && near(xy[0], 320.0f) && near(xy[1], 240.0f));
        assert(!memcmp(uv, glow_uv, sizeof uv));
    }

    /* The same cells away from the centre (the atlas's cross is also the
     * HUD's medkit), other cells at the centre, and a large sprite stay. */
    float medkit_xy[4] = {40.0f, 60.0f, 48.0f, 68.0f}, cross_uv[4] = {0.3125f, 0.125f, 0.375f, 0.25f};
    memcpy(uv, cross_uv, sizeof uv);
    assert(!black_reticle_reshape(medkit_xy, uv, BLACK_RETICLE_RING) && !memcmp(uv, cross_uv, sizeof uv));
    memcpy(xy, dot_xy, sizeof xy);
    float far_xy[4] = {20.0f, 20.0f, 28.0f, 30.7f};
    memcpy(uv, dot_uv, sizeof uv);
    assert(!black_reticle_reshape(far_xy, uv, BLACK_RETICLE_RING));
    assert(!black_reticle_reshape(far_xy, uv, BLACK_RETICLE_CROSS));
    memcpy(uv, cross_uv, sizeof uv);
    assert(!black_reticle_reshape(xy, uv, BLACK_RETICLE_RING));
    assert(!black_reticle_reshape(xy, uv, BLACK_RETICLE_CROSS));
    float big_xy[4] = {0.0f, 0.0f, 640.0f, 480.0f};
    memcpy(uv, dot_uv, sizeof uv);
    assert(!black_reticle_reshape(big_xy, uv, BLACK_RETICLE_RING));
    assert(!black_reticle_reshape(big_xy, uv, BLACK_RETICLE_CROSS));
    /* A uv off the dot by a texel is another sprite. */
    float off_uv[4] = {0.25f + 1.0f / 512.0f, 0.625f, 0.3125f, 0.75f};
    assert(!black_reticle_reshape(xy, off_uv, BLACK_RETICLE_RING));

    /* The Mods row: six values, 0.10.0's order: the dot by default, the
     * shapes, then Auto. */
    assert(black_mod_levels(BLACK_MOD_RETICLE) == BLACK_RETICLE_AUTO);
    assert(!strcmp(black_mod_value_name(BLACK_MOD_RETICLE, 0), "DOT"));
    assert(!strcmp(black_mod_value_name(BLACK_MOD_RETICLE, BLACK_RETICLE_CHEVRONS), "CHEVRONS"));
    assert(!strcmp(black_mod_value_name(BLACK_MOD_RETICLE, BLACK_RETICLE_CROSS), "CROSS"));
    assert(BLACK_RETICLE_CROSS == 3 && BLACK_RETICLE_AUTO == 5);
    assert(!strcmp(black_mod_value_name(BLACK_MOD_RETICLE, BLACK_RETICLE_AUTO), "AUTO"));
    assert(!strcmp(black_mod_value_name(BLACK_MOD_RETICLE, 9), "DOT"));
    assert(black_mod_step(BLACK_MOD_RETICLE, BLACK_RETICLE_CHEVRONS, 0, 1) == BLACK_RETICLE_AUTO);
    /* 0.10.1's numbering: its chevron became the dot, its crosshair the cross. */
    assert(black_mod_reticle_legacy(3) == BLACK_RETICLE_CROSS && black_mod_reticle_legacy(5) == BLACK_RETICLE_DOT);
    assert(black_mod_reticle_legacy(6) == BLACK_RETICLE_CROSS && black_mod_reticle_legacy(7) == BLACK_RETICLE_AUTO);
    assert(black_mod_reticle_legacy(8) == -1 && black_mod_reticle_legacy(-1) == -1);
    assert(black_mod_step(BLACK_MOD_RETICLE, BLACK_RETICLE_AUTO, 0, 1) == BLACK_RETICLE_DOT);
    assert(black_mod_step(BLACK_MOD_RETICLE, BLACK_RETICLE_DOT, 1, 0) == BLACK_RETICLE_RING);
    /* Auto itself is not a shape the quad rewrite draws. */
    memcpy(xy, dot_xy, sizeof xy); memcpy(uv, dot_uv, sizeof uv);
    assert(!black_reticle_reshape(xy, uv, BLACK_RETICLE_AUTO));

    /* Weapon names from the table's base-40 keys, as the sniper mod writes them. */
    {
        const char *in = "BG1_SNR";
        unsigned long long key = 0;
        for (int i = 0; i < 12; ++i) {
            char ch = i < (int)strlen(in) ? in[i] : ' ';
            int c = ch == ' ' ? 0 : ch == '_' ? 39 : ch >= '0' && ch <= '9' ? 3 + ch - '0' : 13 + ch - 'A';
            key = key * 40 + (unsigned long long)c;
        }
        char name[13];
        black_reticle_weapon_name(key, name);
        assert(!strcmp(name, "BG1_SNR"));
    }
    /* Auto: a ring for shotguns and launchers, the dot for the sniper, a
     * cross for the rest and for names it does not know. */
    assert(black_reticle_auto("BG1_SHG") == BLACK_RETICLE_RING && black_reticle_auto("BG1_RM1") == BLACK_RETICLE_RING);
    assert(black_reticle_auto("BG1_GRL") == BLACK_RETICLE_RING && black_reticle_auto("BG1_RPG") == BLACK_RETICLE_RING);
    assert(black_reticle_auto("BG1_SNR") == BLACK_RETICLE_DOT && black_reticle_auto("SNR") == BLACK_RETICLE_DOT);
    assert(black_reticle_auto("BG1_AK1") == BLACK_RETICLE_CROSS && black_reticle_auto("BG1_PST") == BLACK_RETICLE_CROSS);
    assert(black_reticle_auto("BG1_MP1") == BLACK_RETICLE_CROSS && black_reticle_auto("BG1_HVY") == BLACK_RETICLE_CROSS);
    assert(black_reticle_auto("") == BLACK_RETICLE_CROSS && black_reticle_auto(NULL) == BLACK_RETICLE_CROSS);

    puts("Reticle shape: dot kept by default, every shape a grown atlas cell, painted cells in empty blocks, other sprites untouched, Auto by weapon: PASS");
    return 0;
}
