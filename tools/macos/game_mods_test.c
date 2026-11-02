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
    BlackModState state = {0};
    mods[BLACK_MOD_GOD_MODE] = mods[BLACK_MOD_MEDKITS] = mods[BLACK_MOD_GRENADES] = 1;
    const BlackModRevision *r = play(1, 300);
    black_mod_play(m, r, mods, &state);
    float health = black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH);
    uint16_t grenades; memcpy(&grenades, m + PLAYER + BLACK_MOD_GRENADE, 2);
    if (health < 1199.5f || health > 1200.5f) return 5;
    if (m[PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_MEDKIT] != 3 || grenades != 9) return 6;
    /* Hard and Black Ops use the 750 scale; a dead player stays dead. */
    r = play(3, 10); black_mod_play(m, r, mods, &state);
    health = black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH);
    if (health < 749.5f || health > 750.5f) return 7;
    r = play(2, 0); black_mod_play(m, r, mods, &state);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 0) return 8;
    /* Never above the original maximum, never in menus or loading. */
    r = play(0, 1300); black_mod_play(m, r, mods, &state);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 1300) return 9;
    r = play(0, 100); w32(GAME + 0x20220 + 0x4C + 0x1A8, 0x1C); black_mod_play(m, r, mods, &state);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 100 || m[PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_MEDKIT]) return 10;
    r = play(0, 100); w32(usa->player, 0); black_mod_play(m, r, mods, &state);
    if (black_mod_rf(m, PLAYER + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH) != 100) return 11;
    /* Off means untouched. */
    int none[BLACK_MOD_COUNT] = {0};
    r = play(0, 100); black_mod_play(m, r, none, &state);
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
        if (!black_mod_info[i].key || strncmp(black_mod_info[i].key, "mod_", 4) || !black_mod_info[i].label ||
            black_mod_info[i].levels < 1) return 24;

    /* God Mode holds the damage calls' byte and clears only its own. */
    const uint32_t actor = PLAYER + BLACK_MOD_ACTOR;
    int god[BLACK_MOD_COUNT] = {0}; god[BLACK_MOD_GOD_MODE] = 1;
    state.invulnerable = 0;
    r = play(1, 300); black_mod_play(m, r, god, &state);
    if (m[actor + BLACK_MOD_INVULNERABLE] != 1 || state.invulnerable != actor) return 25;
    black_mod_play(m, r, none, &state);
    if (m[actor + BLACK_MOD_INVULNERABLE] || state.invulnerable) return 26;
    m[actor + BLACK_MOD_INVULNERABLE] = 1; /* a script's */
    black_mod_play(m, r, none, &state);
    if (m[actor + BLACK_MOD_INVULNERABLE] != 1) return 27;
    r = play(1, 300); w32(actor + BLACK_MOD_DYING, 1); black_mod_play(m, r, god, &state);
    if (m[actor + BLACK_MOD_INVULNERABLE]) return 28;

    /* One-Hit Kill: the player's hit on another soldier starts from a sliver;
     * a hit that took nothing gives the health back. */
    enum { SOLDIER = 0x380000, WEAPON = 0x3A0000, AMMO = 0x3A1000, ALT = 0x3A2000 };
    int kill[BLACK_MOD_COUNT] = {0}; kill[BLACK_MOD_ONE_HIT_KILL] = 1;
    memset(m, 0, MEMORY);
    black_mod_wf(m, SOLDIER + BLACK_MOD_HEALTH, 750);
    BlackModDoomed doomed = black_mod_doom(m, actor, SOLDIER, actor, kill);
    if (doomed.victim != SOLDIER || black_mod_rf(m, SOLDIER + BLACK_MOD_HEALTH) != BLACK_MOD_SLIVER) return 29;
    black_mod_spare(m, doomed);
    if (fabsf(black_mod_rf(m, SOLDIER + BLACK_MOD_HEALTH) - 750) > 0.01f) return 30;
    doomed = black_mod_doom(m, actor, SOLDIER, actor, kill);
    black_mod_wf(m, SOLDIER + BLACK_MOD_HEALTH, -20); w32(SOLDIER + BLACK_MOD_DYING, 1);
    black_mod_spare(m, doomed);
    if (black_mod_rf(m, SOLDIER + BLACK_MOD_HEALTH) != -20) return 31;
    w32(SOLDIER + BLACK_MOD_DYING, 0); black_mod_wf(m, SOLDIER + BLACK_MOD_HEALTH, 750);
    if (black_mod_doom(m, actor, SOLDIER, SOLDIER + 4, kill).victim || black_mod_doom(m, actor, actor, actor, kill).victim ||
        black_mod_doom(m, actor, SOLDIER, actor, none).victim || black_mod_rf(m, SOLDIER + BLACK_MOD_HEALTH) != 750) return 32;

    /* Infinite Ammo puts back the magazine and reserve; Rapid Fire shortens
     * the wait a shot left; neither touches another soldier's weapon. */
    int ammo[BLACK_MOD_COUNT] = {0}; ammo[BLACK_MOD_FULL_AMMO] = 1; ammo[BLACK_MOD_RAPID_FIRE] = 3;
    memset(m, 0, MEMORY);
    w32(WEAPON + BLACK_MOD_WEAPON_OWNER, actor); w32(WEAPON + BLACK_MOD_WEAPON_AMMO, AMMO); w32(WEAPON + BLACK_MOD_WEAPON_ALT_AMMO, ALT);
    w32(AMMO + BLACK_MOD_AMMO_TOTAL, 90); uint16_t clip = 30; memcpy(m + AMMO + BLACK_MOD_AMMO_CLIP, &clip, 2);
    w32(ALT + BLACK_MOD_AMMO_TOTAL, 3);
    BlackModShot shot = black_mod_before_shot(m, actor, WEAPON, ammo);
    w32(AMMO + BLACK_MOD_AMMO_TOTAL, 89); clip = 29; memcpy(m + AMMO + BLACK_MOD_AMMO_CLIP, &clip, 2);
    black_mod_wf(m, WEAPON + BLACK_MOD_WEAPON_WAIT, 0.4f);
    black_mod_after_shot(m, shot, ammo);
    memcpy(&clip, m + AMMO + BLACK_MOD_AMMO_CLIP, 2);
    if (black_mod_r32(m, AMMO + BLACK_MOD_AMMO_TOTAL) != 90 || clip != 30) return 33;
    if (fabsf(black_mod_rf(m, WEAPON + BLACK_MOD_WEAPON_WAIT) - 0.1f) > 1e-6f) return 34;
    /* No shot this update: the wait counts down as the game's. */
    shot = black_mod_before_shot(m, actor, WEAPON, ammo);
    black_mod_wf(m, WEAPON + BLACK_MOD_WEAPON_WAIT, 0.3f);
    black_mod_after_shot(m, shot, ammo);
    if (black_mod_rf(m, WEAPON + BLACK_MOD_WEAPON_WAIT) != 0.3f) return 35;
    w32(WEAPON + BLACK_MOD_WEAPON_OWNER, SOLDIER);
    if (black_mod_before_shot(m, actor, WEAPON, ammo).weapon || black_mod_before_shot(m, actor, WEAPON, none).weapon) return 36;

    /* No Recoil zeroes the player's decay factor only. */
    int recoil[BLACK_MOD_COUNT] = {0}; recoil[BLACK_MOD_NO_RECOIL] = 1;
    if (black_mod_recoil_decay(actor, actor, 0.8f, recoil) != 0 || black_mod_recoil_decay(actor, SOLDIER, 0.8f, recoil) != 0.8f ||
        black_mod_recoil_decay(actor, actor, 0.8f, none) != 0.8f) return 37;

    /* Levels: switches flip, Rapid Fire steps 0-3, Game Speed walks
     * 50-75-100-150-200% with saved 0 as 100%. */
    if (black_mod_step(BLACK_MOD_GOD_MODE, 0, 0, 1) != 1 || black_mod_step(BLACK_MOD_GOD_MODE, 1, 1, 0) != 1 ||
        black_mod_step(BLACK_MOD_GOD_MODE, 1, -1, 0) != 0) return 38;
    if (black_mod_step(BLACK_MOD_RAPID_FIRE, 0, 1, 0) != 1 || black_mod_step(BLACK_MOD_RAPID_FIRE, 3, 1, 0) != 3 ||
        black_mod_step(BLACK_MOD_RAPID_FIRE, 3, 0, 1) != 0 || strcmp(black_mod_value_name(BLACK_MOD_RAPID_FIRE, 2), "3X")) return 39;
    int speed = 0, seen[5];
    for (int i = 0; i < 5; ++i) { seen[i] = black_mod_game_speed(speed); speed = black_mod_step(BLACK_MOD_GAME_SPEED, speed, 1, 0); }
    if (seen[0] != 100 || seen[1] != 150 || seen[2] != 200 || seen[3] != 200) return 40;
    speed = 0;
    for (int i = 0; i < 3; ++i) speed = black_mod_step(BLACK_MOD_GAME_SPEED, speed, -1, 0);
    if (black_mod_game_speed(speed) != 50 || strcmp(black_mod_value_name(BLACK_MOD_GAME_SPEED, speed), "50%")) return 41;
    if (black_mod_game_speed(black_mod_step(BLACK_MOD_GAME_SPEED, black_mod_step(BLACK_MOD_GAME_SPEED, 0, 0, 1), 0, 1)) != 200 ||
        black_mod_step(BLACK_MOD_GAME_SPEED, 4, 0, 1) != 1 || black_mod_game_speed(9) != 100 ||
        strcmp(black_mod_value_name(BLACK_MOD_GAME_SPEED, 0), "100%") || strcmp(black_mod_value_name(BLACK_MOD_NO_RECOIL, 1), "ON")) return 42;
    free(m);
    puts("game mods: revisions, live values, god mode, one-hit kill, ammo, rapid fire, recoil, speed, progress, spawn and silencer verified");
    return 0;
}
