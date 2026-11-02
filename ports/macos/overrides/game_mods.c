/* Mods page hooks in this runtime's native C ABI, on the game's own thread:
 * live player values per world step, progress flags per front-end tick, the
 * spawn/silencer/recoil points the build overlay inserts and the bullet hit
 * and weapon update it renames (USA code only). */
#include "recomp_types.h"
#include "game_mods.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *guest(void) { return (uint8_t *)XBOX_PTR(0); }
static BlackModState play_state;

extern void black_OriginalBulletHit(void);
extern void black_OriginalWeaponUpdate(void);

static const BlackModRevision *revision(void)
{
    static int checked;
    static const BlackModRevision *found;
    if (!checked) {
        checked = 1;
        uint32_t timestamp = MEM32(BLACK_MOD_XBE_TIMESTAMP);
        found = black_mod_revision(timestamp);
        /* The data writes need the revision's addresses; the code hooks also
         * need the generated code to be that revision. */
        if (found && !found->code_hooks) found = NULL;
        black_ModsSetRevision(found ? found->name : "UNSUPPORTED", found != NULL);
        fprintf(stderr, "[MODS] XBE timestamp %08X: %s\n", timestamp,
            found ? found->name : "unsupported revision, mods stay off");
    }
    return found;
}

static void enabled(int *mods)
{
    for (int i = 0; i < BLACK_MOD_COUNT; ++i) mods[i] = black_SettingsMod(i);
}

void black_ModsPlay(void)
{
    const BlackModRevision *r = revision();
    if (!r) return;
    int mods[BLACK_MOD_COUNT];
    enabled(mods);
    black_mod_play(guest(), r, mods, &play_state);
    static int log = -1;
    static uint32_t logged;
    if (log < 0) log = getenv("BLACK_STATE_LOG") != NULL; /* launcher --state-log */
    uint32_t frame = MEM32(0x2F4368), p = MEM32(r->player);
    if (log && p && frame - logged >= 150 && black_mod_in_play(guest(), r)) {
        logged = frame;
        uint32_t actor = p + BLACK_MOD_ACTOR, weapon = MEM32(actor + 0x29C), ammo = weapon ? MEM32(weapon + BLACK_MOD_WEAPON_AMMO) : 0;
        fprintf(stderr, "[MODS-STATE] frame=%u health=%.1f max=%.1f medkits=%u grenades=%u silver=%u m16a2=%u mission=%u"
            " clip=%u reserve=%u kick=%.4f invulnerable=%u\n",
            frame, MEMF(actor + BLACK_MOD_HEALTH), black_mod_max_health(guest(), r),
            MEM8(actor + BLACK_MOD_MEDKIT), MEM16(p + BLACK_MOD_GRENADE),
            MEM8(p + BLACK_MOD_SILVER), MEM8(p + BLACK_MOD_M16), MEM8(p + BLACK_MOD_MISSION),
            ammo ? MEM16(ammo + BLACK_MOD_AMMO_CLIP) : 0, ammo ? MEM32(ammo + BLACK_MOD_AMMO_TOTAL) : 0,
            MEMF(actor + BLACK_MOD_RECOIL_KICK), MEM8(actor + BLACK_MOD_INVULNERABLE));
    }
}

void black_ModsFrontEnd(void)
{
    const BlackModRevision *r = revision();
    if (!r) return;
    int mods[BLACK_MOD_COUNT];
    enabled(mods);
    black_mod_progress(guest(), r, mods);
}

void black_ModsSpawn(uint32_t player)
{
    if (!revision()) return;
    int mods[BLACK_MOD_COUNT];
    enabled(mods);
    black_mod_spawn(guest(), player, mods);
    if (mods[BLACK_MOD_AMMO] || mods[BLACK_MOD_M16A2])
        fprintf(stderr, "[MODS] spawn mission=%u silver=%u m16a2=%u\n", MEM8(player + BLACK_MOD_MISSION),
            MEM8(player + BLACK_MOD_SILVER), MEM8(player + BLACK_MOD_M16));
}

void black_ModsSilencer(uint32_t item)
{
    if (!black_SettingsMod(BLACK_MOD_SILENCER) || !revision()) return;
    int mods[BLACK_MOD_COUNT] = {0};
    mods[BLACK_MOD_SILENCER] = 1;
    black_mod_silencer(guest(), item, mods);
}

/* The player's actor, or 0 outside a supported world. */
static uint32_t player_actor(void)
{
    const BlackModRevision *r = revision();
    uint32_t p = r ? MEM32(r->player) : 0;
    return p ? p + BLACK_MOD_ACTOR : 0;
}

/* A soldier's bullet hit (ecx the victim, the sixth of seven stack arguments
 * the attacker; ret 0x1C): One-Hit Kill. */
void sub_001E74B0(void)
{
    if (!black_SettingsMod(BLACK_MOD_ONE_HIT_KILL)) { black_OriginalBulletHit(); return; }
    int mods[BLACK_MOD_COUNT];
    enabled(mods);
    BlackModDoomed doomed = black_mod_doom(guest(), player_actor(), g_ecx, MEM32(g_esp + 0x18), mods);
    black_OriginalBulletHit();
    black_mod_spare(guest(), doomed);
    if (doomed.victim) {
        static int logged;
        if (logged++ < 20) fprintf(stderr, "[MODS] one-hit kill: victim %08X health %.1f -> %.1f dying=%u\n",
            doomed.victim, doomed.health, MEMF(doomed.victim + BLACK_MOD_HEALTH), MEM32(doomed.victim + BLACK_MOD_DYING));
    }
}

/* A weapon's update (its first stack argument the weapon; ret 0x20):
 * Infinite Ammo and Rapid Fire for the player's. */
void sub_001EEFD0(void)
{
    static int log = -1;
    if (log < 0) log = getenv("BLACK_STATE_LOG") != NULL; /* launcher --state-log: each of the player's shots */
    if (!log && !black_SettingsMod(BLACK_MOD_FULL_AMMO) && !black_SettingsMod(BLACK_MOD_RAPID_FIRE)) {
        black_OriginalWeaponUpdate();
        return;
    }
    int mods[BLACK_MOD_COUNT];
    enabled(mods);
    uint32_t weapon = MEM32(g_esp + 4), actor = player_actor();
    float rest = MEMF(weapon + BLACK_MOD_WEAPON_WAIT);
    BlackModShot shot = black_mod_before_shot(guest(), actor, weapon, mods);
    black_OriginalWeaponUpdate();
    float wait = MEMF(weapon + BLACK_MOD_WEAPON_WAIT);
    black_mod_after_shot(guest(), shot, mods);
    if (log && actor && MEM32(weapon + BLACK_MOD_WEAPON_OWNER) == actor && wait > rest) {
        uint32_t ammo = MEM32(weapon + BLACK_MOD_WEAPON_AMMO);
        uint32_t view = MEM32(actor + 0x324); /* the view the kick is added to, its pitch at +0x1C */
        fprintf(stderr, "[MODS-SHOT] frame=%u clip=%u total=%d wait=%.3f->%.3f kick=%.4f pitch=%.4f\n", MEM32(0x2F4368),
            ammo ? MEM16(ammo + BLACK_MOD_AMMO_CLIP) : 0, ammo ? (int32_t)MEM32(ammo + BLACK_MOD_AMMO_TOTAL) : 0,
            wait, MEMF(weapon + BLACK_MOD_WEAPON_WAIT), MEMF(actor + BLACK_MOD_RECOIL_KICK), view ? MEMF(view + 0x1C) : 0.0f);
    }
}

/* The player's update at 0x1E5FB7 (ebx the actor being updated): the factor
 * its view kick decays by this step. */
float black_ModsRecoilDecay(uint32_t updated, float decay)
{
    if (!black_SettingsMod(BLACK_MOD_NO_RECOIL)) return decay;
    int mods[BLACK_MOD_COUNT];
    enabled(mods);
    return black_mod_recoil_decay(player_actor(), updated, decay, mods);
}

/* Percent of the game's speed, for the world timer (game_timing.c). */
int black_ModsGameSpeed(void)
{ return revision() ? black_mod_game_speed(black_SettingsMod(BLACK_MOD_GAME_SPEED)) : 100; }

/* The game's own frame number (0x2F4368, USA), for the FPS counter the
 * Cocoa overlay samples; it advances once per finished game frame. Zero
 * until the game thread has recognised its executable. */
uint32_t black_GameFrameCount(void) { return black_ModsSupported() > 0 ? MEM32(0x2F4368) : 0; }
