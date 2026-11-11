/* A bonus weapon at a mission's start: the Mods page's sniper in the first
 * mission and the launcher's BLACK_DEV_WEAPON for research runs. Nothing is
 * created here. The title builds each player's start-loadout record
 * (sub_001B7800), and its own state machine (sub_001DEC70) then streams the
 * weapon banks, checks the names, creates and equips the weapons and adds the
 * record's ammunition. This hook runs in that machine's state 2, after the
 * record is filled and before the M16A2/first-mission overrides, and puts the
 * requested name in the record's second slot with its clip and reserve. The
 * level's standard primary weapon stays as it ships. A live checkpoint's start
 * (Continue Mission, Continue) keeps the record the checkpoint carries, and a
 * revision the Mods page does not support (or has not checked yet, -1) gets
 * nothing: the record's addresses are the USA ones. */
#define RECOMP_GENERATED_CODE 1
#include "recomp_types.h"
#include "game_weapons.h"
#include "game_mods.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The first mission's bonus weapon: the name the weapon table carries (the
 * bank prefix included) and how many magazines of reserve it is granted. */
#define BLACK_WEAPON_SNIPER "BG1_SNR"
#define BLACK_WEAPON_SNIPER_CLIPS 5u

static char s_code[13];
static int s_configured, s_enabled;

static void configure(void)
{
    const char *value;
    size_t length;
    if (s_configured) return;
    s_configured = 1;
    value = getenv("BLACK_DEV_WEAPON");
    if (!value || !*value) return;
    while (*value == ' ' || *value == '\t') value++;
    length = strlen(value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) length--;
    if (!length || length >= sizeof s_code) return;
    memcpy(s_code, value, length);
    s_code[length] = 0;
    s_enabled = 1;
}

/* One line whenever the outcome for a player changes: granted, kept by a live
 * checkpoint, or refused. State 2 repeats each tick while a bank streams. */
static int changed(uint32_t player, int outcome)
{
    static uint32_t last_player;
    static int last_outcome;
    if (last_outcome == outcome && last_player == player) return 0;
    last_outcome = outcome;
    last_player = player;
    return 1;
}

/* State 2 of sub_001DEC70, the player's record in ebx: it runs once per load,
 * and again each tick while the primary weapon's bank streams. The write is
 * idempotent. */
void black_WeaponsLoadout(uint32_t player)
{
    uint8_t *m = (uint8_t *)XBOX_PTR(0);
    uint32_t world = MEM32(BLACK_WEAPON_WORLD), manager, table, primary;
    const char *code;
    unsigned level, stage;
    int type;
    configure();
    if (!world || !player || black_ModsSupported() <= 0) return;
    level = MEM8(world + BLACK_WEAPON_LEVEL);
    stage = MEM8(world + BLACK_WEAPON_STAGE);
    if (s_enabled) {
        /* The table's names carry the bank prefix: SNR is BG1_SNR. */
        manager = MEM32(BLACK_WEAPON_MANAGER);
        table = manager ? MEM32(manager + BLACK_WEAPON_TABLE) : 0;
        if (!table) return;
        if (black_weapon_find(m, table, s_code) < 0 && strncmp(s_code, "BG1_", 4) != 0) {
            char full[13];
            snprintf(full, sizeof full, "BG1_%s", s_code);
            if (black_weapon_find(m, table, full) >= 0) memcpy(s_code, full, sizeof full);
        }
        code = s_code;
    } else if (black_SettingsMod(BLACK_MOD_SNIPER) && level == 0) {
        code = BLACK_WEAPON_SNIPER;
    } else {
        return;
    }
    if (!black_weapon_fresh_start(m)) {
        if (changed(player, 2))
            fprintf(stderr, "[WEAPONS] level=%u stage=%u player %08X: a live checkpoint keeps its loadout; %s not granted\n",
                    level, stage, player, code);
        return;
    }
    type = black_weapon_record(m, player, code, BLACK_WEAPON_SNIPER_CLIPS);
    if (type >= 0) {
        if (changed(player, 1))
            fprintf(stderr, "[WEAPONS] level=%u stage=%u player %08X: %s (type %d) is the second loadout weapon\n",
                    level, stage, player, code, type);
        return;
    }
    if (changed(player, 3)) {
        primary = black_weapon_r32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_PRIMARY) |
            black_weapon_r32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_PRIMARY + 4);
        fprintf(stderr, "[WEAPONS] level=%u stage=%u player %08X: no %s in the weapon table or no primary loadout (%08X); not granted\n",
                level, stage, player, code, primary);
    }
}
