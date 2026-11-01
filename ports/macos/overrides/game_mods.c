/* Mods page hooks in this runtime's native C ABI, on the game's own thread:
 * live player values per world step, progress flags per front-end tick and
 * the spawn/silencer points the build overlay inserts (USA code only). */
#include "recomp_types.h"
#include "game_mods.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>

static uint8_t *guest(void) { return (uint8_t *)XBOX_PTR(0); }

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
    black_mod_play(guest(), r, mods);
    static int log = -1;
    static uint32_t logged;
    if (log < 0) log = getenv("BLACK_STATE_LOG") != NULL; /* launcher --state-log */
    uint32_t frame = MEM32(0x2F4368), p = MEM32(r->player);
    if (log && p && frame - logged >= 150 && black_mod_in_play(guest(), r)) {
        logged = frame;
        fprintf(stderr, "[MODS-STATE] frame=%u health=%.1f max=%.1f medkits=%u grenades=%u silver=%u m16a2=%u mission=%u\n",
            frame, MEMF(p + BLACK_MOD_ACTOR + BLACK_MOD_HEALTH), black_mod_max_health(guest(), r),
            MEM8(p + BLACK_MOD_ACTOR + BLACK_MOD_MEDKIT), MEM16(p + BLACK_MOD_GRENADE),
            MEM8(p + BLACK_MOD_SILVER), MEM8(p + BLACK_MOD_M16), MEM8(p + BLACK_MOD_MISSION));
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

/* The game's own frame number (0x2F4368, USA), for the FPS counter the
 * Cocoa overlay samples; it advances once per finished game frame. Zero
 * until the game thread has recognised its executable. */
uint32_t black_GameFrameCount(void) { return black_ModsSupported() > 0 ? MEM32(0x2F4368) : 0; }
