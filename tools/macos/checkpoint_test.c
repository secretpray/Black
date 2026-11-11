/* The Saves page's per-slot view of the title's checkpoints: the newest
 * readable record carrying the slot's save stamp and player name, and its
 * removal. Every slot of one profile has the same name, so the stamp tells
 * them apart. The title menu's resume item and the restart request's armed
 * check reach a synthetic guest image; the files stay in a private folder. */
#include <stddef.h>
#include "test_temp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

ptrdiff_t g_xbox_mem_offset;
#include "../../ports/macos/overrides/game_checkpoint.inc"

static void set_slot(uint8_t *profile, int slot, const char *name, uint32_t stamp)
{
    uint8_t *q = profile + slot * BLACK_PROFILE_SLOT;
    memcpy(q + BLACK_PROFILE_NAME, name, strlen(name));
    memcpy(q + BLACK_PROFILE_STAMP, &stamp, 4);
}

static int write_record(const uint8_t *profile, const char *name, uint32_t stamp,
    uint32_t difficulty, uint32_t level, uint32_t stage, uint32_t unit, time_t when, int damage)
{
    BlackCheckpoint cp;
    char path[BLACK_CP_PATH_MAX];
    struct timeval times[2] = {{when, 0}, {when, 0}};
    memset(&cp, 0, sizeof cp);
    cp.magic = 0x50434C42u;
    cp.version = 2;
    cp.profile_stamp = stamp;
    cp.difficulty = difficulty;
    cp.level = level;
    cp.stage = stage;
    cp.unit = unit;
    memcpy(cp.name, name, strlen(name));
    cp.campaign_hash = black_cp_hash(profile + BLACK_PROFILE_RECORDS, 1024u);
    black_cp_record_path(&cp, path, sizeof path);
    if (!black_cp_write(path, &cp, NULL)) return 0;
    if (damage) {
        FILE *file = fopen(path, "r+b");
        if (!file) return 0;
        fseek(file, (long)offsetof(BlackCheckpoint, unit), SEEK_SET);
        fputc((int)unit + 1, file);
        fclose(file);
    }
    return utimes(path, times) == 0;
}

static int same(const BlackCheckpointInfo *info, uint32_t d, uint32_t l, uint32_t s, uint32_t u)
{
    return info->difficulty == d && info->level == l && info->stage == s && info->unit == u;
}

static int file_exists(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0;
}

static void record_path(const char *name, uint32_t stamp, uint32_t difficulty,
    uint32_t level, uint32_t stage, char *path, size_t size)
{
    BlackCheckpoint cp;
    memset(&cp, 0, sizeof cp);
    cp.profile_stamp = stamp;
    cp.difficulty = difficulty;
    cp.level = level;
    cp.stage = stage;
    memcpy(cp.name, name, strlen(name));
    black_cp_record_path(&cp, path, size);
}

int main(void)
{
    char root[] = "/tmp/black-checkpoints-XXXXXX";
    uint8_t profile[BLACK_PROFILE_SIZE];
    BlackCheckpointInfo info;
    const char *name = "KELLAR J.";
    const uint32_t stamp0 = 0x3372F7F2u, stamp1 = 0x3372F7EFu, stranger = 0xDEADBEEFu;
    memset(profile, 0, sizeof profile);
    /* Two saved slots of one profile: same name, different stamps; and an
     * empty slot that still keeps the old name and stamp. */
    set_slot(profile, 0, name, stamp0);
    set_slot(profile, 1, name, stamp1);
    set_slot(profile, 2, name, 0);
    profile[BLACK_PROFILE_RECORDS] = 1;
    profile[BLACK_PROFILE_SLOT + BLACK_PROFILE_RECORDS] = 2;
    if (!black_test_temp_dir(root)) return 1;
    setenv("XDG_DATA_HOME", root, 1);
    black_cp_root_ready();
    if (black_CheckpointForSlot(profile, 0, &info)) return 2; /* nothing yet */
    if (!write_record(profile, name, stamp0, 1, 1, 2, 4, 1000, 0)) return 3;
    if (!write_record(profile, name, stamp0, 2, 3, 1, 9, 2000, 0)) return 4; /* newest */
    if (!write_record(profile, name, stamp0, 0, 5, 1, 9, 3000, 1)) return 5; /* bad sum */
    if (!write_record(profile, name, stamp1, 1, 2, 1, 3, 1500, 0)) return 6;
    if (!write_record(profile, name, stranger, 0, 7, 1, 7, 4000, 0)) return 7;
    if (!black_CheckpointForSlot(profile, 0, &info) || !same(&info, 2, 3, 1, 9)) {
        fprintf(stderr, "slot0 difficulty=%u level=%u stage=%u unit=%u\n",
            info.difficulty, info.level, info.stage, info.unit);
        return 8;
    }
    if (!black_CheckpointForSlot(profile, 1, &info) || !same(&info, 1, 2, 1, 3)) return 9;
    /* A slot without a stamp, and a stamp from another launch, show nothing. */
    if (black_CheckpointForSlot(profile, 2, &info)) return 10;
    if (black_CheckpointForSlot(profile, 3, &info) || black_CheckpointRemoveSlot(profile, 3)) return 11;
    /* Every file of slot 0 goes, also the unreadable one; the other slot's
     * record and the stranger's stay. */
    if (black_CheckpointRemoveSlot(profile, 0) != 3) return 12;
    if (black_CheckpointForSlot(profile, 0, &info)) return 13;
    if (!black_CheckpointForSlot(profile, 1, &info) || !same(&info, 1, 2, 1, 3)) return 14;
    if (black_CheckpointRemoveSlot(profile, 2) != 0) return 15;
    if (black_CheckpointRemoveSlot(profile, 1) != 1 || black_CheckpointForSlot(profile, 1, &info)) return 16;
    /* A profile save moves this slot's record to the slot's new save stamp
     * and leaves no copy under the old one; the neighbouring slot is not
     * touched even though it carries the same name. */
    {
        BlackCheckpoint cp;
        BlackCpRebind rebind;
        uint8_t slot_name[80] = {0};
        char old_path[BLACK_CP_PATH_MAX], new_path[BLACK_CP_PATH_MAX];
        const uint32_t saved = 0x11111111u;
        memcpy(slot_name, name, strlen(name));
        if (!write_record(profile, name, stamp1, 1, 2, 1, 3, 1500, 0)) return 17;
        memset(&cp, 0, sizeof cp);
        cp.magic = 0x50434C42u;
        cp.version = 2;
        cp.profile_stamp = stamp0;
        cp.difficulty = 1;
        cp.level = 1;
        cp.stage = 2;
        cp.unit = 4;
        cp.campaign_hash = black_cp_hash(profile + BLACK_PROFILE_RECORDS, 1024u);
        memcpy(cp.name, name, strlen(name));
        record_path(name, stamp0, 1, 1, 2, old_path, sizeof old_path);
        record_path(name, saved, 1, 1, 2, new_path, sizeof new_path);
        if (!black_cp_write(old_path, &cp, NULL)) return 18;
        rebind.name = slot_name;
        rebind.memory_stamp = stamp0;
        rebind.disk_stamp = stamp0;
        rebind.saved_stamp = saved;
        rebind.campaign = cp.campaign_hash;
        black_cp_each(black_cp_rebind_visit, &rebind);
        if (!file_exists(new_path) || file_exists(old_path)) {
            fprintf(stderr, "rebind new=%d old=%d\n%s\n%s\n", file_exists(new_path), file_exists(old_path), new_path, old_path);
            return 19;
        }
        if (black_CheckpointForSlot(profile, 0, &info)) return 20;
        if (!black_CheckpointForSlot(profile, 1, &info) || !same(&info, 1, 2, 1, 3)) return 21;
        /* A save that replaced the campaign results retires the record. */
        profile[BLACK_PROFILE_RECORDS + 1] = 9;
        cp.unit = 5;
        if (!black_cp_write(old_path, &cp, NULL)) return 22;
        rebind.campaign = black_cp_hash(profile + BLACK_PROFILE_RECORDS, 1024u);
        black_cp_each(black_cp_rebind_visit, &rebind);
        if (file_exists(old_path)) return 23;
    }
    /* A synthetic mission: capture, the armed and unarmed loads, F9's
     * restart, the title's own restarts, a damaged file and a profile save
     * in the same launch (the title keeps the old date in memory). */
    {
        uint8_t *ram = calloc(1, BLACK_CP_RAM_SIZE);
        BlackCheckpointContext context;
        char path[BLACK_CP_PATH_MAX], moved[BLACK_CP_PATH_MAX];
        const uint32_t manager = 0x340000u, profile_at = 0x300000u, controller = 0x380000u;
        const uint32_t core = 0x200000u, main = 0x240000u, header = 0x280000u;
        const uint32_t guns = 0x290000u, gun_header = 0x291000u, table = 0x292000u;
        const uint32_t inventory = 0x293000u, objectives = 0x294000u, backup = 0x296000u;
        const uint32_t descriptor[2] = {0x295000u, 0x295020u}, difficulty = 1, saved = 0x44444444u;
        const uint32_t health = 0x42C80000u, hurt = 0x41200000u, units = 9, none = 0xFFFFFFFFu;
        FILE *file;
        if (!ram) return 24;
        g_xbox_mem_offset = (ptrdiff_t)ram;
        black_cp_w32(0x2D19B4u, manager);
        black_cp_w32(manager, profile_at);
        black_cp_w32(profile_at, stamp0);
        memcpy(ram + profile_at + 8u, name, strlen(name) + 1);
        memcpy(ram + profile_at + 92u, profile + BLACK_PROFILE_RECORDS, 1024u);
        black_cp_w32(0x2D45D4u, core);
        black_cp_w32(0x2D45F0u, main);
        ram[core + 0x5AECu] = 3; /* level (2 is the loading state) */
        ram[core + 0x5AEDu] = 1; /* the only stage a mission has */
        black_cp_w32(core + 0x5AF4u, header);
        ram[header + 0x11u] = (uint8_t)units;
        black_cp_w32(main + 0x2014Cu, difficulty);
        black_cp_w32(0x2D12D0u, guns);
        black_cp_w32(guns, gun_header);
        ram[gun_header + 1u] = 3;
        black_cp_w32(gun_header + 4u, table);
        for (uint32_t i = 0; i < 3; ++i) memset(ram + table + i * 32u, (int)(0xA0 + i), 8);
        black_cp_w32(core + 0x440u, inventory);
        black_cp_w32(core + 0x934u, 2);
        black_cp_w32(core + 0x930u, objectives);
        black_cp_w32(0x2F3BFCu, backup);
        for (uint32_t i = 0; i < 2; ++i) {
            memset(ram + descriptor[i], (int)(0x10 + i), 8);
            black_cp_w32(objectives + i * 12u, descriptor[i]);
            black_cp_w32(backup + i * 12u, descriptor[i]);
            black_cp_w32(backup + i * 12u + 4u, 5 + i);
            black_cp_w32(backup + i * 12u + 8u, 1);
        }
        memset(ram + core + 0x3F0u, 0xA1, 8); /* the player's weapon: gun 1 */
        black_cp_w32(core + 0x3F0u + 16u, health);
        ram[inventory] = 1; ram[inventory + 1] = 1;
        /* The title captures at unit 4. */
        ram[0x2F3BF8u] = 1;
        black_cp_w32(0x2A7EDCu, 4);
        black_cp_event(1u, 0u);
        if (!black_cp_context(&context, 0) || !black_cp_path(&context, path, sizeof path) || !file_exists(path)) return 25;
        /* Later in the run: hurt, an objective counted, a gun picked up. */
        black_cp_w32(core + 0x3F0u + 16u, hurt);
        black_cp_w32(backup + 4u, 9);
        ram[inventory + 2] = 1;
        /* Outside a mission, or with a run that has no checkpoint, F9 asks
         * for nothing; a load nobody asked for restores nothing. */
        black_cp_restart_ask();
        if (black_cp_restart_take(0)) return 26;
        ram[0x2F3BF8u] = 0;
        black_cp_restart_ask();
        if (black_cp_restart_take(1)) return 27;
        black_cp_w32(0x2A7EDCu, none);
        black_cp_load(core);
        if (black_cp_r32(core + 0x3F0u + 16u) != hurt || ram[0x2F3BF8u] || black_cp_r32(0x2A7EDCu) != none) return 28;
        /* F9 in the run: armed, the title's restart keeps the file, the
         * reload restores the capture. */
        ram[0x2F3BF8u] = 1;
        black_cp_restart_ask();
        if (black_cp_restart_take(1) != BLACK_CP_RESTART_ASK || black_cp_armed.armed) return 29; /* asks, arms nothing */
        if (black_cp_restart_take(1)) return 30; /* only once */
        black_cp_restart_confirm();
        if (black_cp_restart_take(0)) return 57; /* confirmed outside a mission: nothing */
        black_cp_restart_confirm();
        if (black_cp_restart_take(1) != BLACK_CP_RESTART_GO || !black_cp_armed.armed) return 58;
        black_cp_event(3u, 0u);
        if (!file_exists(path)) return 42;
        ram[0x2F3BF8u] = 0; black_cp_w32(0x2A7EDCu, none); /* what RestartLevel does */
        black_cp_load(core);
        if (black_cp_r32(core + 0x3F0u + 16u) != health || black_cp_r32(backup + 4u) != 5 ||
            ram[inventory + 2] || black_cp_r32(0x2A7EDCu) != 4 || ram[main + 0x2020Du] != 4 ||
            !ram[0x2F3BF8u] || black_cp_armed.armed) return 43;
        /* A death's Continue reloads with the live checkpoint: the file is
         * not read again. */
        black_cp_w32(core + 0x3F0u + 16u, hurt);
        black_cp_arm(3, difficulty);
        black_cp_load(core);
        if (black_cp_r32(core + 0x3F0u + 16u) != hurt || black_cp_armed.armed) return 44;
        /* A damaged file changes nothing, even armed. */
        {
            BlackCheckpoint *cp = malloc(sizeof *cp);
            if (!cp || !black_cp_read(path, cp)) return 45;
            file = fopen(path, "wb");
            if (!file) return 46;
            fwrite(cp, 1, sizeof *cp - 1, file);
            fclose(file);
            ram[0x2F3BF8u] = 0;
            black_cp_arm(3, difficulty);
            black_cp_load(core);
            if (black_cp_r32(core + 0x3F0u + 16u) != hurt || ram[0x2F3BF8u]) return 47;
            ram[0x2F3BF8u] = 1;
            black_cp_restart_ask();
            if (black_cp_restart_take(1)) return 48; /* F9 would restart from the start */
            black_cp_restart_confirm();
            if (black_cp_restart_take(1) || black_cp_armed.armed) return 59;
            if (!black_cp_write(path, cp, NULL)) return 49;
            free(cp);
        }
        /* A profile save in this launch: the file follows the saved date,
         * the title keeps playing under the old one, and the next capture,
         * F9 and load still reach it. */
        black_cp_w32(controller + 1316u, 0);
        black_cp_w32(controller + 4712u, 38);
        black_cp_w32(controller + 4712u + 0xC8u, saved);
        memcpy(ram + controller + 4712u + 0xD0u, name, strlen(name) + 1);
        memcpy(ram + controller + 4712u + 0x124u, ram + profile_at + 92u, 1024u);
        black_cp_event(4u, controller);
        if (file_exists(path) || black_cp_r32(profile_at) != stamp0) return 50;
        if (!black_cp_path(&context, moved, sizeof moved) || !file_exists(moved) || !strcmp(path, moved)) return 51;
        black_cp_restart_ask();
        if (black_cp_restart_take(1) != BLACK_CP_RESTART_ASK) return 52;
        black_cp_restart_confirm();
        if (black_cp_restart_take(1) != BLACK_CP_RESTART_GO) return 60;
        ram[0x2F3BF8u] = 0;
        black_cp_load(core);
        if (black_cp_r32(core + 0x3F0u + 16u) != health || !ram[0x2F3BF8u]) return 53;
        black_cp_w32(0x2A7EDCu, 5);
        black_cp_event(1u, 0u);
        {
            BlackCheckpoint cp;
            if (!black_cp_read(moved, &cp) || cp.unit != 5 || cp.profile_stamp != saved || file_exists(path)) return 54;
        }
        /* The title's own Restart, unarmed, starts over and removes the file. */
        black_cp_event(3u, 0u);
        if (file_exists(moved)) return 55;
        g_xbox_mem_offset = 0;
        memset(black_cp_alias, 0, sizeof black_cp_alias);
        free(ram);
    }
    /* The title menu's resume item: the active slot's record, valid for this
     * profile's campaign results and for a mission the title has unlocked;
     * prepare hands its place to the title's own start action. */
    {
        uint8_t *ram = calloc(1, BLACK_CP_RAM_SIZE);
        BlackCheckpoint cp;
        const uint32_t manager = 0x340000u, save_manager = 0x320000u;
        const uint32_t profile_at = 0x300000u, main = 0x240000u;
        uint32_t slot = 0;
        char path[BLACK_CP_PATH_MAX];
        if (!ram) return 31;
        g_xbox_mem_offset = (ptrdiff_t)ram;
        memcpy(ram + 0x2D19B4u, &manager, 4);
        memcpy(ram + manager, &profile_at, 4);
        memcpy(ram + 0x2D19BCu, &save_manager, 4);
        memcpy(ram + save_manager + 0x524u, &slot, 4);
        memcpy(ram + save_manager + 0x1268u + BLACK_PROFILE_STAMP, &stamp0, 4);
        memcpy(ram + save_manager + 0x1268u + BLACK_PROFILE_NAME, name, strlen(name));
        memcpy(ram + 0x2D45F0u, &main, 4);
        /* The live profile: name at +8, its campaign results at +92. */
        memcpy(ram + profile_at, &stamp0, 4);
        memcpy(ram + profile_at + 8u, name, strlen(name) + 1);
        memcpy(ram + profile_at + 92u, profile + BLACK_PROFILE_RECORDS, 1024u);
        ram[0x288648u] = 1; /* mission 0 is level 1... */
        ram[0x2D0D9Eu + 0x100u] = 1; /* ...on difficulty 1 */
        memset(&cp, 0, sizeof cp);
        cp.magic = 0x50434C42u;
        cp.version = 2;
        cp.profile_stamp = stamp0;
        cp.difficulty = 1;
        cp.level = 1;
        cp.stage = 2;
        cp.unit = 4;
        cp.campaign_hash = black_cp_hash(ram + profile_at + 92u, 1024u);
        memcpy(cp.name, name, strlen(name));
        black_cp_record_path(&cp, path, sizeof path);
        if (!black_cp_write(path, &cp, NULL)) return 32;
        if (!black_cp_resume_offer()) return 33;
        if (!black_cp_resume_prepare()) return 34;
        if (ram[main + 0x2020Cu] != 1 || ram[main + 0x2020Du] != 4 || ram[main + 0x2020Eu] != 2) return 35;
        if (black_cp_r32(main + 0x2014Cu) != 1) return 36;
        if (ram[0x2F3BF8u]) return 37;
        if (!black_cp_armed.armed || black_cp_armed.level != 1 || black_cp_armed.difficulty != 1) return 56;
        black_cp_armed.armed = 0;
        /* A mission the title has not unlocked offers nothing... */
        ram[0x2D0D9Eu + 0x100u] = 0;
        if (black_cp_resume_offer() || black_cp_resume_prepare()) return 38;
        ram[0x2D0D9Eu + 0x100u] = 1;
        /* ...nor a record from a profile whose campaign results changed... */
        ram[profile_at + 92u] = 9;
        if (black_cp_resume_offer()) return 39;
        ram[profile_at + 92u] = 1;
        /* ...nor an active slot with no record of its own. */
        slot = 2;
        memcpy(ram + save_manager + 0x524u, &slot, 4);
        if (black_cp_resume_offer()) return 40;
        slot = 0;
        memcpy(ram + save_manager + 0x524u, &slot, 4);
        if (!black_cp_resume_offer()) return 41;
        unlink(path);
        g_xbox_mem_offset = 0;
        free(ram);
    }
    puts("Checkpoint files per save slot, capture and armed restore, restarts, same-launch profile save: PASS");
    return 0;
}
