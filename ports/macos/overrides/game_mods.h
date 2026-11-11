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

/* Mods page order; settings keys are stable across releases. Player mods come
 * before BLACK_MOD_UNLOCK_MISSIONS, which starts the Unlocks group. */
enum { BLACK_MOD_GOD_MODE, BLACK_MOD_MEDKITS, BLACK_MOD_GRENADES, BLACK_MOD_FULL_AMMO, BLACK_MOD_AMMO,
       BLACK_MOD_RAPID_FIRE, BLACK_MOD_NO_RECOIL, BLACK_MOD_ONE_HIT_KILL, BLACK_MOD_GAME_SPEED, BLACK_MOD_SILENCER,
       BLACK_MOD_UNLOCK_MISSIONS, BLACK_MOD_BLACK_OPS, BLACK_MOD_M16A2, BLACK_MOD_BFG, BLACK_MOD_SNIPER,
       BLACK_MOD_COUNT };

/* levels: the highest saved value; 1 is an on/off switch. */
typedef struct { const char *key, *label; int levels; } BlackModInfo;
static const BlackModInfo black_mod_info[BLACK_MOD_COUNT] = {
    {"mod_god_mode", "God Mode", 1}, {"mod_infinite_medkits", "Infinite Medkits", 1},
    {"mod_infinite_grenades", "Infinite Grenades", 1}, {"mod_full_ammo", "Infinite Ammo", 1},
    {"mod_infinite_ammo", "Silver Weapons", 1}, {"mod_rapid_fire", "Rapid Fire", 3},
    {"mod_no_recoil", "No Recoil", 1}, {"mod_one_hit_kill", "One-Hit Kill", 1},
    {"mod_game_speed", "Game Speed", 4}, {"mod_always_silenced", "Always Silenced", 1},
    {"mod_unlock_missions", "Unlock All Missions", 1}, {"mod_unlock_black_ops", "Unlock Black Ops Difficulty", 1},
    {"mod_m16a2", "M16A2 at Mission Start", 1}, {"mod_bfg", "M249 in the First Mission", 1},
    {"mod_sniper", "Sniper in the First Mission", 1},
};

/* Game Speed: saved 0 is the game's own speed, so Defaults and old settings
 * files keep it; the menu steps through the percentages in order. */
static const int black_mod_speed_percent[5] = {100, 50, 75, 150, 200};
static const int black_mod_speed_order[5] = {1, 2, 0, 3, 4};

static inline int black_mod_levels(int mod) { return mod >= 0 && mod < BLACK_MOD_COUNT ? black_mod_info[mod].levels : 0; }

/* The value a left/right press (direction -1/+1) or a confirm (cycle) gives. */
static inline int black_mod_step(int mod, int value, int direction, int cycle)
{
    int levels = black_mod_levels(mod);
    if (levels == 1) return cycle ? !value : direction > 0;
    int position = value;
    if (mod == BLACK_MOD_GAME_SPEED)
        for (int i = 0; i <= levels; ++i) if (black_mod_speed_order[i] == value) position = i;
    position += cycle ? 1 : direction;
    if (cycle && position > levels) position = 0;
    position = position < 0 ? 0 : position > levels ? levels : position;
    return mod == BLACK_MOD_GAME_SPEED ? black_mod_speed_order[position] : position;
}

/* What the menu shows for a saved value. */
static inline const char *black_mod_value_name(int mod, int value)
{
    static const char *const speed[5] = {"100%", "50%", "75%", "150%", "200%"};
    static const char *const rapid[4] = {"OFF", "2X", "3X", "4X"};
    if (value < 0 || value > black_mod_levels(mod)) value = 0;
    if (mod == BLACK_MOD_GAME_SPEED) return speed[value];
    if (mod == BLACK_MOD_RAPID_FIRE) return rapid[value];
    return value ? "ON" : "OFF";
}

/* Percent of the game's speed for the saved Game Speed value. */
static inline int black_mod_game_speed(int value)
{ return value > 0 && value < 5 ? black_mod_speed_percent[value] : 100; }

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
/* Character fields (the player's actor and every soldier), from TGP482
 * BLACK-XBERecomp PR 141 (ported from Black-Android). */
#define BLACK_MOD_DYING 0x384u        /* u32, not 0 once killed */
#define BLACK_MOD_INVULNERABLE 0x8F2u /* byte the scripts set: the damage calls take nothing */
/* Weapon: owner (an actor), the wait before the next shot, the main and the
 * under-barrel ammo records; a record's reserve and magazine. */
#define BLACK_MOD_WEAPON_OWNER 0xF0u
#define BLACK_MOD_WEAPON_WAIT 0xDCu
#define BLACK_MOD_WEAPON_AMMO 0xF4u
#define BLACK_MOD_WEAPON_ALT_AMMO 0xF8u
#define BLACK_MOD_AMMO_TOTAL 0x14u /* u32 */
#define BLACK_MOD_AMMO_CLIP 0x18u  /* u16 */
/* Recoil: the view kick the player's update decays by a factor a step. */
#define BLACK_MOD_RECOIL_KICK 0x4C4u
#define BLACK_MOD_RECOIL_DECAY 0x4C8u
/* One-Hit Kill leaves a soldier this much health before the player's hit. */
#define BLACK_MOD_SLIVER 0.001f

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

/* The actor whose invulnerability byte God Mode set: the scripts set that
 * byte too, so only ours is cleared again. */
typedef struct { uint32_t invulnerable; } BlackModState;

/* Live player values, once per world step. A dead player stays dead. God
 * Mode also holds the byte the game's own damage calls test, so no single
 * hit, however large, gets through. */
static inline void black_mod_play(uint8_t *m, const BlackModRevision *r, const int *mods, BlackModState *state)
{
    if (!r || !black_mod_in_play(m, r)) return;
    uint32_t p = black_mod_r32(m, r->player), actor = p + BLACK_MOD_ACTOR;
    int alive = !black_mod_r32(m, actor + BLACK_MOD_DYING);
    if (state->invulnerable && state->invulnerable != actor) state->invulnerable = 0; /* another world */
    if (mods[BLACK_MOD_GOD_MODE] && alive) {
        float health = black_mod_rf(m, actor + BLACK_MOD_HEALTH), max = black_mod_max_health(m, r);
        if (max > 0 && health > 0 && health < max) black_mod_wf(m, actor + BLACK_MOD_HEALTH, max);
        m[actor + BLACK_MOD_INVULNERABLE] = 1;
        state->invulnerable = actor;
    } else if (state->invulnerable == actor) {
        m[actor + BLACK_MOD_INVULNERABLE] = 0;
        state->invulnerable = 0;
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

/* One-Hit Kill, around a soldier's bullet hit 0x1E74B0 (ecx the victim, its
 * sixth argument the attacker): a hit of the player's on someone else starts
 * from a sliver of health, so any damage kills; a hit that took nothing gives
 * the health back. */
typedef struct { uint32_t victim; float health; } BlackModDoomed;

static inline BlackModDoomed black_mod_doom(uint8_t *m, uint32_t actor, uint32_t victim, uint32_t attacker, const int *mods)
{
    BlackModDoomed none = {0, 0};
    if (!mods[BLACK_MOD_ONE_HIT_KILL] || !actor || !victim || attacker != actor || victim == actor ||
        black_mod_r32(m, victim + BLACK_MOD_DYING)) return none;
    float health = black_mod_rf(m, victim + BLACK_MOD_HEALTH);
    if (!(health > BLACK_MOD_SLIVER)) return none;
    black_mod_wf(m, victim + BLACK_MOD_HEALTH, BLACK_MOD_SLIVER);
    BlackModDoomed kept = {victim, health};
    return kept;
}

static inline void black_mod_spare(uint8_t *m, BlackModDoomed kept)
{
    if (!kept.victim || black_mod_r32(m, kept.victim + BLACK_MOD_DYING)) return;
    float health = black_mod_rf(m, kept.victim + BLACK_MOD_HEALTH);
    if (health > 0) black_mod_wf(m, kept.victim + BLACK_MOD_HEALTH, kept.health - (BLACK_MOD_SLIVER - health));
}

/* Around the weapon update 0x1EEFD0 (its first argument the weapon), for the
 * player's weapon: Infinite Ammo puts back what a shot or a reload took from
 * the magazine and the reserve; Rapid Fire shortens the wait a shot leaves. */
typedef struct { uint32_t weapon, ammo, total, rounds; uint16_t clip; } BlackModShot;

static inline uint32_t black_mod_rounds(const uint8_t *m, uint32_t weapon)
{
    uint32_t rounds = 0, at[2] = {BLACK_MOD_WEAPON_AMMO, BLACK_MOD_WEAPON_ALT_AMMO};
    for (int i = 0; i < 2; ++i) {
        uint32_t ammo = black_mod_r32(m, weapon + at[i]);
        if (ammo) rounds += black_mod_r32(m, ammo + BLACK_MOD_AMMO_TOTAL);
    }
    return rounds;
}

static inline BlackModShot black_mod_before_shot(const uint8_t *m, uint32_t actor, uint32_t weapon, const int *mods)
{
    BlackModShot shot = {0, 0, 0, 0, 0};
    if (!actor || !weapon || black_mod_r32(m, weapon + BLACK_MOD_WEAPON_OWNER) != actor ||
        (!mods[BLACK_MOD_FULL_AMMO] && !mods[BLACK_MOD_RAPID_FIRE])) return shot;
    shot.weapon = weapon;
    shot.rounds = black_mod_rounds(m, weapon);
    uint32_t ammo = black_mod_r32(m, weapon + BLACK_MOD_WEAPON_AMMO);
    if (mods[BLACK_MOD_FULL_AMMO] && ammo) {
        shot.ammo = ammo;
        shot.total = black_mod_r32(m, ammo + BLACK_MOD_AMMO_TOTAL);
        memcpy(&shot.clip, m + ammo + BLACK_MOD_AMMO_CLIP, 2);
    }
    return shot;
}

static inline void black_mod_after_shot(uint8_t *m, BlackModShot shot, const int *mods)
{
    if (!shot.weapon) return;
    int fired = black_mod_rounds(m, shot.weapon) < shot.rounds;
    if (shot.ammo && black_mod_r32(m, shot.weapon + BLACK_MOD_WEAPON_AMMO) == shot.ammo) {
        if (black_mod_r32(m, shot.ammo + BLACK_MOD_AMMO_TOTAL) < shot.total)
            memcpy(m + shot.ammo + BLACK_MOD_AMMO_TOTAL, &shot.total, 4);
        uint16_t clip; memcpy(&clip, m + shot.ammo + BLACK_MOD_AMMO_CLIP, 2);
        if (clip < shot.clip) memcpy(m + shot.ammo + BLACK_MOD_AMMO_CLIP, &shot.clip, 2);
    }
    int faster = mods[BLACK_MOD_RAPID_FIRE] + 1;
    float wait = black_mod_rf(m, shot.weapon + BLACK_MOD_WEAPON_WAIT);
    if (fired && faster > 1 && faster <= 4 && wait > 0) black_mod_wf(m, shot.weapon + BLACK_MOD_WEAPON_WAIT, wait / faster);
}

/* No Recoil, in the player's update 0x1E5FA0 (ebx the actor): the factor the
 * view kick decays by becomes 0, so a shot's kick never reaches the view. */
static inline float black_mod_recoil_decay(uint32_t actor, uint32_t updated, float decay, const int *mods)
{ return mods[BLACK_MOD_NO_RECOIL] && actor && updated == actor ? 0.0f : decay; }
#endif
