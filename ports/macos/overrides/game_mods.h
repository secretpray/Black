/* BLACK cheats and modifications for the Mods page. Addresses come from the
 * local trainer/XBE study (EvolutionX +3/+9 trainers, the title's own unlock
 * routines) and are kept per XBE revision, chosen by the header timestamp at
 * 0x10114. Everything here works on guest memory through a base pointer, so
 * the policy is testable without a retail game. */
#ifndef BLACK_GAME_MODS_H
#define BLACK_GAME_MODS_H
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Mods page order; settings keys are stable across releases. */
enum { BLACK_MOD_GOD_MODE, BLACK_MOD_MEDKITS, BLACK_MOD_GRENADES, BLACK_MOD_AMMO, BLACK_MOD_SILENCER,
       BLACK_MOD_UNLOCK_MISSIONS, BLACK_MOD_BLACK_OPS, BLACK_MOD_M16A2, BLACK_MOD_BFG, BLACK_MOD_COUNT };

typedef struct { const char *key, *label; } BlackModInfo;
static const BlackModInfo black_mod_info[BLACK_MOD_COUNT] = {
    {"mod_god_mode", "God Mode"}, {"mod_infinite_medkits", "Infinite Medkits"},
    {"mod_infinite_grenades", "Infinite Grenades"}, {"mod_infinite_ammo", "Infinite Ammo (Silver Weapons)"},
    {"mod_always_silenced", "Always Silenced"}, {"mod_unlock_missions", "Unlock All Missions"},
    {"mod_unlock_black_ops", "Unlock Black Ops Difficulty"}, {"mod_m16a2", "M16A2 at Mission Start"},
    {"mod_bfg", "BFG (M249) Code"},
};

/* The world player (`P`), session (difficulty at +0x2014C), live progress
 * block (`G`, saved into the profile) and the two health scales 1/max. The
 * generated Mac code is lifted from the USA XBE; a European disc is
 * normalized to that executable, so only USA has the code hooks. */
typedef struct {
    const char *name;
    uint32_t timestamp, player, game, progress, health_scale[2];
    int code_hooks;
} BlackModRevision;
static const BlackModRevision black_mod_revisions[] = {
    {"USA", 0x43C2A9DFu, 0x2D45D4u, 0x2D45F0u, 0x2D0D80u, {0x295E48u, 0x295E44u}, 1},
    {"EUROPE", 0x43C2A843u, 0x2D45F4u, 0x2D4610u, 0x2D0DA0u, {0x295E68u, 0x295E64u}, 0},
};
#define BLACK_MOD_XBE_TIMESTAMP 0x10114u

/* Player layout, the same in both revisions: the actor is P+0x30. */
#define BLACK_MOD_ACTOR 0x30u
#define BLACK_MOD_HEALTH 0x2F0u    /* float, on the actor */
#define BLACK_MOD_MEDKIT 0x8E1u    /* byte, on the actor; pickups stop at 3 */
#define BLACK_MOD_GRENADE 0x2B6u   /* u16, on P */
#define BLACK_MOD_MISSION 0x5AECu  /* byte, on P; campaign missions are < 0x32 */
#define BLACK_MOD_SILVER 0x5CB0u   /* byte, on P: magazine refills, 999 shown */
#define BLACK_MOD_M16 0x5CB1u      /* byte, on P: starting weapon becomes M16A2 */
#define BLACK_MOD_SILENCER_FLAG 0x108u /* byte, on a weapon item */
#define BLACK_MOD_MEDKIT_MAX 3u
#define BLACK_MOD_GRENADE_COUNT 9u

static inline uint32_t black_mod_r32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
static inline float black_mod_rf(const uint8_t *m, uint32_t a) { float v; memcpy(&v, m + a, 4); return v; }
static inline void black_mod_wf(uint8_t *m, uint32_t a, float v) { memcpy(m + a, &v, 4); }

static inline const BlackModRevision *black_mod_revision(uint32_t timestamp)
{
    for (unsigned i = 0; i < sizeof black_mod_revisions / sizeof black_mod_revisions[0]; ++i)
        if (black_mod_revisions[i].timestamp == timestamp) return &black_mod_revisions[i];
    return NULL;
}

/* Initialized 3D play only: the condition the timer hook uses to pause. */
static inline int black_mod_in_play(const uint8_t *m, const BlackModRevision *r)
{
    uint32_t p = black_mod_r32(m, r->player), game = black_mod_r32(m, r->game);
    return p && game && black_mod_r32(m, game + 0x20220 + 0x4C + 0x1A8) == 0x37 &&
        (black_mod_rf(m, p + 0x130) != 0 || black_mod_rf(m, p + 0x134) != 0 || black_mod_rf(m, p + 0x138) != 0);
}

/* The health getter 0x13EC0 divides by 1200 on Easy/Normal and 750 above. */
static inline float black_mod_max_health(const uint8_t *m, const BlackModRevision *r)
{
    uint32_t difficulty = black_mod_r32(m, black_mod_r32(m, r->game) + 0x2014C);
    if (difficulty > 3) return 0;
    float scale = black_mod_rf(m, r->health_scale[difficulty >= 2]);
    float max = scale > 0 && isfinite(scale) ? 1.0f / scale : 0;
    return max >= 1 && max <= 100000 ? max : 0;
}

/* Live player values, once per world step. A dead player stays dead. */
static inline void black_mod_play(uint8_t *m, const BlackModRevision *r, const int *mods)
{
    if (!r || !black_mod_in_play(m, r)) return;
    uint32_t p = black_mod_r32(m, r->player), actor = p + BLACK_MOD_ACTOR;
    if (mods[BLACK_MOD_GOD_MODE]) {
        float health = black_mod_rf(m, actor + BLACK_MOD_HEALTH), max = black_mod_max_health(m, r);
        if (max > 0 && health > 0 && health < max) black_mod_wf(m, actor + BLACK_MOD_HEALTH, max);
    }
    if (mods[BLACK_MOD_MEDKITS] && m[actor + BLACK_MOD_MEDKIT] < BLACK_MOD_MEDKIT_MAX)
        m[actor + BLACK_MOD_MEDKIT] = BLACK_MOD_MEDKIT_MAX;
    if (mods[BLACK_MOD_GRENADES]) {
        uint16_t grenades; memcpy(&grenades, m + p + BLACK_MOD_GRENADE, 2);
        if (grenades < BLACK_MOD_GRENADE_COUNT) { grenades = BLACK_MOD_GRENADE_COUNT; memcpy(m + p + BLACK_MOD_GRENADE, &grenades, 2); }
    }
}

/* Progress flags in G, as the title's unreferenced debug routine 0x1F5A40
 * and its Hard reward write them. Only ever set: G is saved into the
 * profile, so turning a mod off never relocks earned progress. */
static inline void black_mod_progress(uint8_t *m, const BlackModRevision *r, const int *mods)
{
    if (!r) return;
    uint32_t g = r->progress;
    int difficulties = mods[BLACK_MOD_BLACK_OPS] ? 4 : 3;
    if (mods[BLACK_MOD_UNLOCK_MISSIONS])
        for (int d = 0; d < difficulties; ++d) {
            m[g + d] = 7; /* last opened mission, where mission select puts the cursor */
            for (int level = 0; level < 8; ++level) m[g + 4 + (d * 8 + level) * 0x20 + 0x1A] = 1;
        }
    if (mods[BLACK_MOD_BLACK_OPS]) {
        m[g + 4 + 24 * 0x20 + 0x1A] = 1; /* G+0x31E: Black Ops mission 1 */
        for (int d = 0; d < 4; ++d) m[g + 0x528 + d] = 1;
    }
    if (mods[BLACK_MOD_BFG]) m[g + 0x52D] = 1; /* what a XXXX-XXXX-XXXX-XXXX profile name sets */
}

/* Player spawn 0x1EE1FF, after the title derived its reward flags. */
static inline void black_mod_spawn(uint8_t *m, uint32_t player, const int *mods)
{
    if (!player || m[player + BLACK_MOD_MISSION] >= 0x32) return;
    if (mods[BLACK_MOD_AMMO]) m[player + BLACK_MOD_SILVER] = 1;
    if (mods[BLACK_MOD_M16A2]) m[player + BLACK_MOD_M16] = 1;
}

/* Weapon update 0x1E61BF, the +9 trainer's silencer point. */
static inline void black_mod_silencer(uint8_t *m, uint32_t item, const int *mods)
{
    if (item && mods[BLACK_MOD_SILENCER]) m[item + BLACK_MOD_SILENCER_FLAG] = 1;
}
#endif
