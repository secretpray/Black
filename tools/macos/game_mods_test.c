#include "../../ports/macos/overrides/game_mods.h"
#include <stdio.h>
#include <stdlib.h>

enum { MEMORY = 0x400000, PLAYER = 0x300000, GAME = 0x340000, ITEM = 0x3F0000 };
static uint8_t *m;
static void w32(uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }

/* A USA world in 3D play on difficulty `d`, health `h`. */
static const BlackModRevision *play(uint32_t d, float h)
{
    memset(m, 0, MEMORY);
    const BlackModRevision *r = black_mod_revision(0x43C2A9DFu);
    w32(r->player, PLAYER); w32(r->game, GAME);
    w32(GAME + 0x2014C, d);
    w32(GAME + 0x20220 + 0x4C + 0x1A8, 0x37);
    black_mod_wf(m, PLAYER + 0x130, 12.5f);
    black_mod_wf(m, r->health_scale[0], 1.0f / 1200);
    black_mod_wf(m, r->health_scale[1], 1.0f / 750);
    black_mod_wf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH, h);
    return r;
}

int main(void)
{
    m = calloc(1, MEMORY);
    if (!m) return 1;
    const BlackModRevision *usa = black_mod_revision(0x43C2A9DFu), *eu = black_mod_revision(0x43C2A843u);
    if (!usa || strcmp(usa->name, "USA") || !usa->code_hooks || usa->player != 0x2D45D4u) return 2;
    /* European data is shifted +0x20 and has no lifted code of its own. */
    if (!eu || eu->code_hooks || eu->player != usa->player + 0x20 || eu->progress != usa->progress + 0x20) return 3;
    if (black_mod_revision(0) || black_mod_revision(0x43C2A9E0u)) return 4;

    int mods[BLACK_MOD_COUNT] = {0};
    mods[BLACK_MOD_GOD_MODE] = mods[BLACK_MOD_MEDKITS] = mods[BLACK_MOD_GRENADES] = 1;
    const BlackModRevision *r = play(1, 300);
    black_mod_play(m, r, mods);
    float health = black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH);
    uint16_t grenades; memcpy(&grenades, m + PLAYER + BLACK_MOD_GRENADE, 2);
    if (health < 1199.5f || health > 1200.5f) return 5;
    if (m[PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_MEDKIT] != 3 || grenades != 9) return 6;
    /* Hard and Black Ops use the 750 scale; a dead player stays dead. */
    r = play(3, 10); black_mod_play(m, r, mods);
    health = black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH);
    if (health < 749.5f || health > 750.5f) return 7;
    r = play(2, 0); black_mod_play(m, r, mods);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 0) return 8;
    /* Never above the original maximum, never in menus or loading. */
    r = play(0, 1300); black_mod_play(m, r, mods);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 1300) return 9;
    r = play(0, 100); w32(GAME + 0x20220 + 0x4C + 0x1A8, 0x1C); black_mod_play(m, r, mods);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 100 || m[PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_MEDKIT]) return 10;
    r = play(0, 100); w32(usa->player, 0); black_mod_play(m, r, mods);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 100) return 11;
    /* Off means untouched. */
    int none[BLACK_MOD_COUNT] = {0};
    r = play(0, 100); black_mod_play(m, r, none);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 100) return 12;

    /* Progress: missions on Easy-Hard; Black Ops adds the fourth difficulty. */
    uint32_t g = usa->progress;
    memset(m, 0, MEMORY);
    mods[BLACK_MOD_UNLOCK_MISSIONS] = 1;
    black_mod_progress(m, usa, mods);
    for (int d = 0; d < 4; ++d)
        for (int level = 0; level < 8; ++level)
            if (m[g + 4 + (d * 8 + level) * 0x20 + 0x1A] != (d < 3)) return 13;
    if (m[g] != 7 || m[g + 2] != 7 || m[g + 3] || m[g + 0x528] || m[g + 0x52D]) return 14;
    memset(m, 0, MEMORY);
    mods[BLACK_MOD_UNLOCK_MISSIONS] = 0; mods[BLACK_MOD_BLACK_OPS] = 1;
    black_mod_progress(m, usa, mods);
    if (m[g + 0x31E] != 1 || m[g + 0x31E + 0x20] || m[g + 0x1E]) return 15;
    for (int d = 0; d < 4; ++d) if (m[g + 0x528 + d] != 1) return 16;
    mods[BLACK_MOD_UNLOCK_MISSIONS] = 1; mods[BLACK_MOD_BFG] = 1;
    black_mod_progress(m, usa, mods);
    if (m[g + 4 + (31 * 0x20) + 0x1A] != 1 || m[g + 3] != 7 || m[g + 0x52D] != 1) return 17;
    /* Turning everything off never relocks earned progress. */
    black_mod_progress(m, usa, none);
    if (m[g + 0x31E] != 1 || m[g + 0x52D] != 1) return 18;
    black_mod_progress(m, NULL, mods);

    /* Spawn rewards: campaign missions only, OR-ed onto the title's own. */
    memset(m, 0, MEMORY);
    mods[BLACK_MOD_AMMO] = mods[BLACK_MOD_M16A2] = 1;
    m[PLAYER + BLACK_MOD_MISSION] = 7;
    black_mod_spawn(m, PLAYER, mods);
    if (m[PLAYER + BLACK_MOD_SILVER] != 1 || m[PLAYER + BLACK_MOD_M16] != 1) return 19;
    memset(m, 0, MEMORY);
    m[PLAYER + BLACK_MOD_MISSION] = 0x32;
    black_mod_spawn(m, PLAYER, mods);
    if (m[PLAYER + BLACK_MOD_SILVER] || m[PLAYER + BLACK_MOD_M16]) return 20;
    m[PLAYER + BLACK_MOD_MISSION] = 0; m[PLAYER + BLACK_MOD_SILVER] = 1;
    black_mod_spawn(m, PLAYER, none);
    if (m[PLAYER + BLACK_MOD_SILVER] != 1 || m[PLAYER + BLACK_MOD_M16]) return 21;
    black_mod_spawn(m, 0, mods);

    mods[BLACK_MOD_SILENCER] = 1;
    black_mod_silencer(m, ITEM, mods);
    if (m[ITEM + BLACK_MOD_SILENCER_FLAG] != 1) return 22;
    m[ITEM + BLACK_MOD_SILENCER_FLAG] = 0;
    black_mod_silencer(m, ITEM, none);
    if (m[ITEM + BLACK_MOD_SILENCER_FLAG]) return 23;
    for (int i = 0; i < BLACK_MOD_COUNT; ++i)
        if (!black_mod_info[i].key || strncmp(black_mod_info[i].key, "mod_", 4) || !black_mod_info[i].label) return 24;
    free(m);
    puts("game mods: revisions, live values, progress, spawn and silencer verified");
    return 0;
}
