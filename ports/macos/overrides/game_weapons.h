#pragma once
/* A bonus weapon at a mission's start (game_weapons.c), and the pure reads it
 * makes: the USA weapon manager and the start-loadout record the title builds
 * at each fresh mission start. The Mods page's sniper and
 * the launcher's BLACK_DEV_WEAPON both write that record, so the game itself
 * streams the weapon's bank, creates and equips it. */
#include <stdint.h>
#include <string.h>

/* The weapon manager: +0 the table. */
#define BLACK_WEAPON_MANAGER 0x2D12D0u
#define BLACK_WEAPON_TABLE 0u
#define BLACK_WEAPON_TABLE_COUNT 1u    /* byte: entries in use */
#define BLACK_WEAPON_TABLE_ENTRIES 4u  /* the entries */
#define BLACK_WEAPON_ENTRY_SIZE 0x20u  /* name key (8 bytes) first */

/* The world: the level and its stage (bytes). */
#define BLACK_WEAPON_WORLD 0x2D45D4u
#define BLACK_WEAPON_LEVEL 0x5AECu
#define BLACK_WEAPON_STAGE 0x5AEDu

/* The title's live checkpoint (docs/VALIDATION.md). While it is set the start
 * state skips sub_001B7800: the record then holds what the checkpoint carries,
 * and the bonus weapon must not replace a weapon the player has since taken. */
#define BLACK_WEAPON_CHECKPOINT_LIVE 0x2F3BF8u

/* The player's start-loadout record (sub_001B7800 fills it from the mission
 * descriptor; sub_001DEC70 streams, creates and equips what it names): the
 * primary and secondary name keys, one ammunition count per type (ten), the
 * clips and the flags. The counts are grants the title adds to the
 * inventory; it caps them itself. */
#define BLACK_WEAPON_RECORD 0x3C0u
#define BLACK_WEAPON_RECORD_PRIMARY 0x00u   /* key low, then high */
#define BLACK_WEAPON_RECORD_SECONDARY 0x08u /* key low, then high */
#define BLACK_WEAPON_RECORD_AMMO 0x14u      /* u32; +ammo_type*4 */
#define BLACK_WEAPON_RECORD_PRIMARY_CLIP 0x3Cu   /* u32 */
#define BLACK_WEAPON_RECORD_SECONDARY_CLIP 0x40u /* u32 */

/* A weapon definition: its clip and its ammunition type. */
#define BLACK_WEAPON_AMMO_TYPES 10
#define BLACK_WEAPON_DATA_CLIP 0x60u
#define BLACK_WEAPON_DATA_AMMO_TYPE 0x64u

static inline uint32_t black_weapon_r32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
static inline uint16_t black_weapon_r16(const uint8_t *m, uint32_t a) { uint16_t v; memcpy(&v, m + a, 2); return v; }
static inline void black_weapon_w32(uint8_t *m, uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }

/* The name key the game keeps: twelve characters of " -/0-9A-Z_" in base 40. */
static inline uint64_t black_weapon_key(const char *name)
{
    uint64_t key = 0;
    int ended = !name;
    for (int i = 0; i < 12; i++) {
        char ch = ' ';
        int c;
        /* Short names stay space-padded: nothing past the terminator is read. */
        if (!ended) {
            ch = name[i];
            if (!ch) { ch = ' '; ended = 1; }
        }
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        c = ch == '-' ? 1 : ch == '/' ? 2 : ch >= '0' && ch <= '9' ? 3 + ch - '0' :
            ch >= 'A' && ch <= 'Z' ? 13 + ch - 'A' : ch == '_' ? 39 : 0;
        key = key * 40 + (uint64_t)c;
    }
    return key;
}

/* 1 when the start state is building a fresh loadout: no live checkpoint. */
static inline int black_weapon_fresh_start(const uint8_t *m)
{
    return m[BLACK_WEAPON_CHECKPOINT_LIVE] == 0;
}

/* The index in the manager's weapon table whose name is `code`, or -1. */
static inline int black_weapon_find(const uint8_t *m, uint32_t table, const char *code)
{
    uint64_t key = black_weapon_key(code);
    uint32_t entries = black_weapon_r32(m, table + BLACK_WEAPON_TABLE_ENTRIES);
    int count = m[table + BLACK_WEAPON_TABLE_COUNT];
    if (!entries || count <= 0 || count > 64) return -1;
    for (int i = 0; i < count; i++) {
        uint32_t e = entries + (uint32_t)i * BLACK_WEAPON_ENTRY_SIZE;
        if ((((uint64_t)black_weapon_r32(m, e + 4) << 32) | black_weapon_r32(m, e)) == key) return i;
    }
    return -1;
}

/* The definition object behind a table type: sub_001B7800 and the loadout's
 * build read its clip at +0x60 and its ammunition type at +0x64. Returns 0
 * for a type the table does not have. */
static inline int black_weapon_definition(const uint8_t *m, uint32_t table, int type,
                                          uint16_t *clip, int *ammo_type)
{
    uint32_t entries = black_weapon_r32(m, table + BLACK_WEAPON_TABLE_ENTRIES);
    int count = m[table + BLACK_WEAPON_TABLE_COUNT];
    uint32_t def;
    int kind;
    if (!entries || type < 0 || type >= count || type >= 64) return 0;
    def = black_weapon_r32(m, entries + (uint32_t)type * BLACK_WEAPON_ENTRY_SIZE + 8u);
    if (!def) return 0;
    kind = (int8_t)m[def + BLACK_WEAPON_DATA_AMMO_TYPE];
    if (kind < 0 || kind >= BLACK_WEAPON_AMMO_TYPES) return 0;
    if (clip) *clip = black_weapon_r16(m, def + BLACK_WEAPON_DATA_CLIP);
    if (ammo_type) *ammo_type = kind;
    return 1;
}

/* Puts `code` in the player's start-loadout record as the secondary weapon,
 * with its clip and `clips` magazines of its own ammunition type; the title's
 * state machine then streams, creates and equips it. Returns the table type,
 * or -1 when nothing was written: the manager or the name is not there yet, or
 * the record has no primary weapon, which the title's own loadout refuses.
 * A count the record already has for the ammunition type is never lowered. */
static inline int black_weapon_record(uint8_t *m, uint32_t player, const char *code, unsigned clips)
{
    uint32_t manager, table, at, amount;
    uint64_t key;
    uint16_t clip;
    int type, ammo_type;
    if (!player || !code || !*code) return -1;
    manager = black_weapon_r32(m, BLACK_WEAPON_MANAGER);
    table = manager ? black_weapon_r32(m, manager + BLACK_WEAPON_TABLE) : 0;
    if (!table) return -1;
    type = black_weapon_find(m, table, code);
    if (type < 0 || !black_weapon_definition(m, table, type, &clip, &ammo_type)) return -1;
    if (!(black_weapon_r32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_PRIMARY) |
          black_weapon_r32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_PRIMARY + 4))) return -1;
    key = black_weapon_key(code);
    black_weapon_w32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_SECONDARY, (uint32_t)key);
    black_weapon_w32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_SECONDARY + 4, (uint32_t)(key >> 32));
    black_weapon_w32(m, player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_SECONDARY_CLIP, clip);
    if (!clips) clips = 1;
    amount = (uint32_t)clip * clips;
    if (amount > 0xFFFFu) amount = 0xFFFFu;
    at = player + BLACK_WEAPON_RECORD + BLACK_WEAPON_RECORD_AMMO + (uint32_t)ammo_type * 4u;
    if (amount > black_weapon_r32(m, at)) black_weapon_w32(m, at, amount);
    return type;
}
