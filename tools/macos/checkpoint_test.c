/* The Saves page's per-slot view of the title's checkpoints: the newest
 * readable record carrying the slot's save stamp and player name, and its
 * removal. Every slot of one profile has the same name, so the stamp tells
 * them apart. Real files in a private data folder; no guest memory is
 * reached. */
#include <stddef.h>
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
    if (!black_cp_write(path, &cp)) return 0;
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
    if (!mkdtemp(root)) return 1;
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
        if (!black_cp_write(old_path, &cp)) return 18;
        rebind.name = slot_name;
        rebind.memory_stamp = stamp0;
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
        if (!black_cp_write(old_path, &cp)) return 22;
        rebind.campaign = black_cp_hash(profile + BLACK_PROFILE_RECORDS, 1024u);
        black_cp_each(black_cp_rebind_visit, &rebind);
        if (file_exists(old_path)) return 23;
    }
    puts("Checkpoint files per save slot, newest record and removal: PASS");
    return 0;
}
