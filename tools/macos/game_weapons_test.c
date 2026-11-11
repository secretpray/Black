#include "../../ports/macos/overrides/game_weapons.h"
#include <stdio.h>
#include <stdlib.h>

/* The reads and the one write game_weapons.c makes before the game gives a
 * weapon: names, the weapon table, a definition's clip and ammunition type,
 * the start-loadout record and the live-checkpoint gate. The addresses are the
 * USA ones the runtime maps. */
enum {
    MEMORY = 0x2E00000,
    PLAYER = 0x100030,
    MANAGER = 0x300000,
    TABLE = 0x320000,
    ENTRIES = 0x360000,
    DEF = 0x480000
};

static uint8_t *m;

static void w32(uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }
static void w16(uint32_t a, uint16_t v) { memcpy(m + a, &v, 2); }

static void entry(int index, const char *name)
{
    uint64_t key = black_weapon_key(name);
    uint32_t e = ENTRIES + (uint32_t)index * BLACK_WEAPON_ENTRY_SIZE;
    w32(e, (uint32_t)key);
    w32(e + 4, (uint32_t)(key >> 32));
}

int main(void)
{
    int type;
    m = calloc(1, MEMORY);
    if (!m) return 1;

    /* Names: twelve characters of " -/0-9A-Z_" in base 40, case folded and
     * space padded. */
    if (black_weapon_key("snr") != black_weapon_key("SNR") ||
        black_weapon_key("PST") == black_weapon_key("SNR") ||
        black_weapon_key("") != black_weapon_key(" ") ||
        black_weapon_key("A") != 13ull * 40ull * 40ull * 40ull * 40ull * 40ull * 40ull * 40ull * 40ull * 40ull * 40ull * 40ull)
        return 2;

    /* The weapon table: three of the game's names, found by their key. */
    w32(BLACK_WEAPON_MANAGER, MANAGER);
    w32(MANAGER + BLACK_WEAPON_TABLE, TABLE);
    m[TABLE + BLACK_WEAPON_TABLE_COUNT] = 3;
    w32(TABLE + BLACK_WEAPON_TABLE_ENTRIES, ENTRIES);
    entry(0, "PST");
    entry(1, "SNR");
    entry(2, "RPG");
    type = black_weapon_find(m, TABLE, "SNR");
    if (type != 1 || black_weapon_find(m, TABLE, "snr") != 1 || black_weapon_find(m, TABLE, "AK1") != -1) return 10;
    m[TABLE + BLACK_WEAPON_TABLE_COUNT] = 0;
    if (black_weapon_find(m, TABLE, "SNR") != -1) return 11;
    m[TABLE + BLACK_WEAPON_TABLE_COUNT] = 3;
    w32(TABLE + BLACK_WEAPON_TABLE_ENTRIES, 0);
    if (black_weapon_find(m, TABLE, "SNR") != -1) return 12;
    w32(TABLE + BLACK_WEAPON_TABLE_ENTRIES, ENTRIES);

    /* The start-loadout record: the definition behind a name gives its clip
     * and ammunition type, and the writer puts the name, its clip and five
     * magazines in the second slot. A larger count the record already grants
     * for that type stays, a name the table lacks and a record without a
     * primary weapon are refused. */
    {
        uint32_t rec = PLAYER + BLACK_WEAPON_RECORD;
        uint64_t key = black_weapon_key("SNR");
        uint16_t clip = 0;
        int ammo_type = -1;
        w32(ENTRIES + BLACK_WEAPON_ENTRY_SIZE + 8, DEF);
        w16(DEF + BLACK_WEAPON_DATA_CLIP, 5);
        m[DEF + BLACK_WEAPON_DATA_AMMO_TYPE] = 7;
        if (!black_weapon_definition(m, TABLE, 1, &clip, &ammo_type) || clip != 5 || ammo_type != 7) return 24;
        if (black_weapon_definition(m, TABLE, 3, &clip, &ammo_type) ||
            black_weapon_definition(m, TABLE, -1, &clip, &ammo_type) ||
            black_weapon_definition(m, TABLE, 0, &clip, &ammo_type)) return 25;
        m[DEF + BLACK_WEAPON_DATA_AMMO_TYPE] = BLACK_WEAPON_AMMO_TYPES;
        if (black_weapon_definition(m, TABLE, 1, &clip, &ammo_type)) return 32;
        m[DEF + BLACK_WEAPON_DATA_AMMO_TYPE] = 7;
        w32(rec + BLACK_WEAPON_RECORD_PRIMARY, 1);
        if (black_weapon_record(m, PLAYER, "SNR", 5) != 1) return 26;
        if (black_weapon_r32(m, rec + BLACK_WEAPON_RECORD_SECONDARY) != (uint32_t)key ||
            black_weapon_r32(m, rec + BLACK_WEAPON_RECORD_SECONDARY + 4) != (uint32_t)(key >> 32) ||
            black_weapon_r32(m, rec + BLACK_WEAPON_RECORD_SECONDARY_CLIP) != 5) return 27;
        if (black_weapon_r32(m, rec + BLACK_WEAPON_RECORD_AMMO + 7 * 4) != 25) return 28;
        w32(rec + BLACK_WEAPON_RECORD_AMMO + 7 * 4, 100);
        if (black_weapon_record(m, PLAYER, "SNR", 5) != 1 ||
            black_weapon_r32(m, rec + BLACK_WEAPON_RECORD_AMMO + 7 * 4) != 100) return 29;
        if (black_weapon_record(m, PLAYER, "AK1", 5) >= 0) return 30;
        w32(rec + BLACK_WEAPON_RECORD_PRIMARY, 0);
        w32(rec + BLACK_WEAPON_RECORD_PRIMARY + 4, 0);
        if (black_weapon_record(m, PLAYER, "SNR", 5) >= 0) return 31;
    }

    /* A live checkpoint keeps its own loadout: only a fresh start is written. */
    if (!black_weapon_fresh_start(m)) return 40;
    m[BLACK_WEAPON_CHECKPOINT_LIVE] = 1;
    if (black_weapon_fresh_start(m)) return 41;
    m[BLACK_WEAPON_CHECKPOINT_LIVE] = 0;

    free(m);
    puts("game weapons: names, table, definitions, the mission loadout record and the checkpoint gate verified");
    return 0;
}
