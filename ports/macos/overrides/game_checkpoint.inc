/* Disk format, writer and loader for the supported retail build's native
 * checkpoint. A POSIX port of portingpete/Black-2006-Recomp
 * src/black_checkpoint.inc (MIT). No guest pointers are serialized. Loading
 * runs after Guns/LevelDat are ready and before either initial streaming unit
 * starts (1EDC90:1EE310), and only for a load that asked for it: the title
 * menu's Continue Mission item or F9 / Back + Y. The title's own starts and
 * restarts begin the mission from its start, as on the console, and its
 * death/Continue keeps owning the in-memory checkpoint. A successful profile
 * autosave (1FE713) stores the surviving checkpoints under the profile's new
 * date, and the Saves page reads and removes each slot's files by its save
 * stamp and player name (every slot of a profile carries the same name). */
#include "game_checkpoint.h"
#include "black_profile.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

#ifndef BLACK_CP_RAM_SIZE
#define BLACK_CP_RAM_SIZE 0x04000000u /* the Xbox's 64 MB address space */
#endif
/* MmAllocateContiguousMemory hands back this window, and the title's mission
 * core and profile manager live in it (core 0x804FD180 at level start). The
 * runtime maps it at the same offset as low RAM, so one base reaches both. */
#ifndef BLACK_CP_WINDOW_BASE
#define BLACK_CP_WINDOW_BASE 0x80000000u
#endif
#ifndef BLACK_CP_WINDOW_SIZE
#define BLACK_CP_WINDOW_SIZE 0x04000000u
#endif
#define BLACK_CP_MAX_OBJECTIVES 4096u
#define BLACK_CP_MAX_GUNS 127u

typedef struct BlackCheckpoint {
    uint32_t magic, version, checksum, profile_stamp;
    uint32_t level, stage, difficulty, unit, objectives, guns;
    uint8_t name[80];
    uint8_t player[80];             /* +0x50 is an allocation, deliberately excluded */
    uint8_t stats[24];              /* 2F3BF4 time, six counters and two shorts */
    uint32_t campaign_hash;
    uint8_t inventory[BLACK_CP_MAX_GUNS];
    uint8_t objective_data[16u * BLACK_CP_MAX_OBJECTIVES]; /* descriptor ID64, counter, state */
} BlackCheckpoint;

typedef struct BlackCheckpointContext {
    uint32_t core, main, profile, header, gun_header;
    uint32_t objective_data, objective_backup, inventory, objective_count, gun_count;
} BlackCheckpointContext;

static char black_cp_root[BLACK_CP_PATH_MAX];
#if defined(__APPLE__)
#define BLACK_CP_MTIME(status) ((status)->st_mtimespec)
#else
#define BLACK_CP_MTIME(status) ((status)->st_mtim)
#endif

static int black_cp_ram_span(uint32_t address, uint32_t bytes)
{
    const uint64_t end = (uint64_t)address + bytes;
    return ((uint64_t)address >= 0x10000u && end <= BLACK_CP_RAM_SIZE) ||
        ((uint64_t)address >= BLACK_CP_WINDOW_BASE &&
         end <= (uint64_t)BLACK_CP_WINDOW_BASE + BLACK_CP_WINDOW_SIZE);
}

static uint8_t *black_cp_memory(uint32_t address)
{
    return (uint8_t *)((uintptr_t)g_xbox_mem_offset + address);
}

static uint32_t black_cp_r32(uint32_t address)
{
    uint32_t value;
    memcpy(&value, black_cp_memory(address), 4);
    return value;
}

static void black_cp_w32(uint32_t address, uint32_t value)
{
    memcpy(black_cp_memory(address), &value, 4);
}

static uint32_t black_cp_hash(const void *data, size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    while (length--) hash = (hash ^ *p++) * 16777619u;
    return hash;
}

/* The title writes a save's date into the slot copy it saves (0x1F1A00 on
 * that buffer, from 0x1FB3F0), never into the profile it plays: until the
 * profile is loaded again the running title knows it by the old date, while
 * the files, the Saves page and the next launch go by the new one. A profile
 * save records that pair here, for this launch only, and every name built
 * from the playing profile goes through it. Game thread only. */
#define BLACK_CP_ALIASES 8u
static struct { uint32_t memory, saved, name; } black_cp_alias[BLACK_CP_ALIASES];
static unsigned black_cp_alias_next;

static uint32_t black_cp_disk_stamp(uint32_t memory, const uint8_t *name)
{
    const uint32_t hash = black_cp_hash(name, 80u);
    for (unsigned i = 0; i < BLACK_CP_ALIASES; ++i)
        if (black_cp_alias[i].saved && black_cp_alias[i].memory == memory && black_cp_alias[i].name == hash)
            return black_cp_alias[i].saved;
    return memory;
}

static void black_cp_alias_set(uint32_t memory, uint32_t saved, const uint8_t *name)
{
    const uint32_t hash = black_cp_hash(name, 80u);
    unsigned at = black_cp_alias_next % BLACK_CP_ALIASES;
    for (unsigned i = 0; i < BLACK_CP_ALIASES; ++i)
        if (black_cp_alias[i].saved && black_cp_alias[i].memory == memory && black_cp_alias[i].name == hash) {
            black_cp_alias[i].saved = saved;
            return;
        }
    black_cp_alias[at].memory = memory;
    black_cp_alias[at].saved = saved;
    black_cp_alias[at].name = hash;
    ++black_cp_alias_next;
}

/* The playing profile's stamp as its files carry it. */
static uint32_t black_cp_profile_stamp(const BlackCheckpointContext *c)
{
    return black_cp_disk_stamp(black_cp_r32(c->profile), black_cp_memory(c->profile + 8u));
}

/* $XDG_DATA_HOME/xboxrecomp/Checkpoints, beside the saves. */
static void black_cp_default_root(void)
{
    const char *data = getenv("XDG_DATA_HOME");
    char base[BLACK_CP_PATH_MAX];
    if (data && *data) snprintf(base, sizeof base, "%s", data);
    else {
        const char *home = getenv("HOME");
        snprintf(base, sizeof base, "%s/.local/share", home && *home ? home : "/tmp");
    }
    snprintf(black_cp_root, sizeof black_cp_root, "%s/xboxrecomp/Checkpoints", base);
}

/* The game thread and the panel both reach the directory. */
static pthread_once_t black_cp_root_once = PTHREAD_ONCE_INIT;
static void black_cp_root_ready(void)
{
    pthread_once(&black_cp_root_once, black_cp_default_root);
}

static int black_cp_mkdirs(void)
{
    char path[BLACK_CP_PATH_MAX];
    if (!black_cp_root[0]) return 1;
    snprintf(path, sizeof path, "%s", black_cp_root);
    for (char *p = path + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(path, 0700) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static void black_cp_sync_parent(const char *path)
{
    char directory[BLACK_CP_PATH_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash) return;
    size_t length = slash == path ? 1 : (size_t)(slash - path);
    memcpy(directory, path, length);
    directory[length] = 0;
    int fd = open(directory, O_RDONLY);
    if (fd >= 0) { fsync(fd); close(fd); }
}

/* The profile alone: enough to identify its resume points before a mission
 * has set a level or stage. Empty/new unsaved profiles have no stable
 * identity across launches. */
static int black_cp_profile_context(BlackCheckpointContext *c)
{
    uint32_t manager;
    memset(c, 0, sizeof(*c));
    manager = black_cp_r32(0x2D19B4u);
    if (!black_cp_ram_span(manager, 4u)) return 0;
    c->profile = black_cp_r32(manager);
    if (!black_cp_ram_span(c->profile, 0x6A8u) ||
        black_cp_memory(c->profile)[1701] || !black_cp_r32(c->profile) ||
        !black_cp_memory(c->profile)[8] || !memchr(black_cp_memory(c->profile + 8u), 0, 80u)) return 0;
    return 1;
}

static int black_cp_context(BlackCheckpointContext *c, int buffers)
{
    uint32_t guns;
    if (!black_cp_profile_context(c)) return 0;
    c->core = black_cp_r32(0x2D45D4u);
    c->main = black_cp_r32(0x2D45F0u);
    if (!black_cp_ram_span(c->core, 0x5CB4u) || !black_cp_ram_span(c->main, 0x21090u)) return 0;
    if (black_cp_memory(c->core)[0x5AECu] > 8u || black_cp_memory(c->core)[0x5AECu] == 2u ||
        black_cp_memory(c->core)[0x5AEDu] != 1u || black_cp_r32(c->main + 0x2014Cu) > 3u) return 0;
    c->header = black_cp_r32(c->core + 0x5AF4u);
    if (!black_cp_ram_span(c->header, 0x13u) ||
        !black_cp_memory(c->header)[0x11] || black_cp_memory(c->header)[0x11] > 127u) return 0;
    if (!buffers) return 1;
    guns = black_cp_r32(0x2D12D0u);
    if (!black_cp_ram_span(guns, 4u)) return 0;
    c->gun_header = black_cp_r32(guns);
    if (!black_cp_ram_span(c->gun_header, 8u)) return 0;
    c->gun_count = black_cp_memory(c->gun_header)[1];
    c->objective_count = black_cp_r32(c->core + 0x934u);
    c->objective_data = black_cp_r32(c->core + 0x930u);
    c->objective_backup = black_cp_r32(0x2F3BFCu);
    c->inventory = black_cp_r32(c->core + 0x440u);
    return c->gun_count && c->gun_count <= BLACK_CP_MAX_GUNS &&
        c->objective_count <= BLACK_CP_MAX_OBJECTIVES &&
        black_cp_ram_span(c->inventory, c->gun_count) &&
        (!c->objective_count ||
         (black_cp_ram_span(c->objective_data, 12u * c->objective_count) &&
          black_cp_ram_span(c->objective_backup, 12u * c->objective_count)));
}

static int black_cp_path(const BlackCheckpointContext *c, char *path, size_t size)
{
    return snprintf(path, size, "%s/%08X-%08X-%u-%u-%u.bin", black_cp_root,
        black_cp_profile_stamp(c), black_cp_hash(black_cp_memory(c->profile + 8u), 80u),
        black_cp_r32(c->main + 0x2014Cu), black_cp_memory(c->core)[0x5AECu],
        black_cp_memory(c->core)[0x5AEDu]) > 0;
}

/* The same name from a record, for copies under another profile date. */
static void black_cp_record_path(const BlackCheckpoint *cp, char *path, size_t size)
{
    snprintf(path, size, "%s/%08X-%08X-%u-%u-%u.bin", black_cp_root, cp->profile_stamp,
        black_cp_hash(cp->name, 80u), cp->difficulty, cp->level, cp->stage);
}

/* `keep` (or NULL for now) is the capture time the file carries: the newest
 * record of a slot is chosen by it, so a copy keeps its original's. */
static int black_cp_write(const char *path, BlackCheckpoint *cp, const struct stat *keep)
{
    char temporary[BLACK_CP_PATH_MAX + 32];
    ssize_t written;
    int fd, ok;
    if (!black_cp_mkdirs()) return 0;
    snprintf(temporary, sizeof temporary, "%s.%ld.tmp", path, (long)getpid());
    cp->checksum = 0;
    cp->checksum = black_cp_hash(cp, sizeof(*cp));
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 0;
    written = write(fd, cp, sizeof(*cp));
    ok = written == (ssize_t)sizeof(*cp) && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok) {
        ok = rename(temporary, path) == 0;
        /* A failed replacement retains the last complete checkpoint. */
        if (ok && keep) {
            struct timespec times[2] = {BLACK_CP_MTIME(keep), BLACK_CP_MTIME(keep)};
            utimensat(AT_FDCWD, path, times, 0);
        }
        if (ok) black_cp_sync_parent(path);
    }
    if (!ok) unlink(temporary);
    return ok;
}

/* One complete record with its own checksum; 0 for anything else. */
static int black_cp_read(const char *path, BlackCheckpoint *cp)
{
    FILE *file = fopen(path, "rb");
    uint32_t checksum;
    int complete;
    if (!file) return 0;
    complete = fread(cp, 1, sizeof(*cp), file) == sizeof(*cp) && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!complete) return 0;
    checksum = cp->checksum;
    cp->checksum = 0;
    if (black_cp_hash(cp, sizeof(*cp)) != checksum) return 0;
    cp->checksum = checksum;
    return 1;
}

static void black_cp_identity(BlackCheckpoint *cp, const BlackCheckpointContext *c)
{
    memset(cp, 0, sizeof(*cp));
    cp->magic = 0x50434C42u; /* BLCP */
    cp->version = 2;
    cp->profile_stamp = black_cp_profile_stamp(c);
    cp->level = black_cp_memory(c->core)[0x5AECu];
    cp->stage = black_cp_memory(c->core)[0x5AEDu];
    cp->difficulty = black_cp_r32(c->main + 0x2014Cu);
    cp->campaign_hash = black_cp_hash(black_cp_memory(c->profile + 92u), 1024u);
    memcpy(cp->name, black_cp_memory(c->profile + 8u), 80);
}

static void black_cp_save(void)
{
    BlackCheckpointContext c;
    BlackCheckpoint *cp;
    char path[BLACK_CP_PATH_MAX];
    const int trace = getenv("RECOMP_BLACK_CHECKPOINT_TRACE") != NULL;
    if (!black_cp_memory(0x2F3BF8u)[0]) {
        if (trace) fprintf(stderr, "[CHECKPOINT_TRACE] capture skipped: the game has not recorded a checkpoint yet\n");
        return;
    }
    if (!black_cp_context(&c, 1)) {
        if (trace) fprintf(stderr, "[CHECKPOINT_TRACE] capture skipped: mission or profile context is not ready\n");
        return;
    }
    cp = (BlackCheckpoint *)calloc(1, sizeof(*cp));
    if (!cp) return;
    black_cp_identity(cp, &c);
    cp->unit = black_cp_r32(0x2A7EDCu);
    if (cp->unit < 2u || cp->unit > black_cp_memory(c.header)[0x11]) {
        if (trace) fprintf(stderr, "[CHECKPOINT_TRACE] capture skipped: unit %u is outside the mission's table\n", cp->unit);
        free(cp); return;
    }
    cp->objectives = c.objective_count;
    cp->guns = c.gun_count;
    memcpy(cp->player, black_cp_memory(c.core + 0x3F0u), 80);
    memcpy(cp->stats, black_cp_memory(0x2F3BF4u), 4);
    memcpy(cp->stats + 4, black_cp_memory(0x2F3C00u), 16);
    memcpy(cp->inventory, black_cp_memory(c.inventory), c.gun_count);
    {
        uint32_t i;
        for (i = 0; i < c.objective_count; ++i) {
            const uint32_t entry = c.objective_backup + i * 12u;
            const uint32_t descriptor = black_cp_r32(entry);
            if (!black_cp_ram_span(descriptor, 22u)) { free(cp); return; }
            memcpy(cp->objective_data + i * 16u, black_cp_memory(descriptor), 8u);
            memcpy(cp->objective_data + i * 16u + 8u, black_cp_memory(entry + 4u), 8u);
        }
    }
    black_cp_path(&c, path, sizeof path);
    {
        const int saved = black_cp_write(path, cp, NULL);
        fprintf(stderr, "[CHECKPOINT] %s level=%u stage=%u difficulty=%u unit=%u bytes=%zu\n",
            saved ? "saved" : "SAVE FAILED", cp->level, cp->stage, cp->difficulty, cp->unit, sizeof(*cp));
    }
    free(cp);
}

/* The record's weapon and health fields are native values: one exact token
 * from the mission's gun table, a supported single-player player struct and
 * objective counters the title could have produced. */
static int black_cp_token(const BlackCheckpointContext *c, const uint8_t *token, int optional)
{
    uint32_t table = black_cp_r32(c->gun_header + 4u), i, matches = 0;
    static const uint8_t empty[8] = {0};
    if (optional && !memcmp(token, empty, 8)) return 1;
    if (!black_cp_ram_span(table, c->gun_count * 32u)) return 0;
    for (i = 0; i < c->gun_count; ++i)
        if (!memcmp(token, black_cp_memory(table + i * 32u), 8)) ++matches;
    return matches == 1u;
}

static int black_cp_valid(const BlackCheckpoint *cp, const BlackCheckpointContext *c)
{
    uint32_t i, value;
    if (cp->magic != 0x50434C42u || cp->version != 2u ||
        cp->profile_stamp != black_cp_profile_stamp(c) ||
        cp->campaign_hash != black_cp_hash(black_cp_memory(c->profile + 92u), 1024u) ||
        memcmp(cp->name, black_cp_memory(c->profile + 8u), 80) ||
        cp->level != black_cp_memory(c->core)[0x5AECu] ||
        cp->stage != black_cp_memory(c->core)[0x5AEDu] ||
        cp->difficulty != black_cp_r32(c->main + 0x2014Cu) ||
        cp->unit < 2u || cp->unit > black_cp_memory(c->header)[0x11] ||
        cp->guns != c->gun_count || cp->objectives != c->objective_count ||
        !black_cp_token(c, cp->player, 0) || !black_cp_token(c, cp->player + 8, 1)) return 0;
    memcpy(&value, cp->player + 72, 4);
    if (value != 0u) return 0; /* supported single-player record */
    memcpy(&value, cp->player + 16, 4);
    if (!value || value >= 0x7F800000u) return 0; /* positive finite native health */
    for (i = 20; i <= 64; i += 4) {
        memcpy(&value, cp->player + i, 4);
        if (value > 65535u) return 0;
    }
    for (i = 0; i < c->objective_count; ++i) {
        const uint32_t descriptor = black_cp_r32(c->objective_data + i * 12u);
        if (!black_cp_ram_span(descriptor, 22u) ||
            memcmp(cp->objective_data + i * 16u, black_cp_memory(descriptor), 8u)) return 0;
        memcpy(&value, cp->objective_data + i * 16u + 8u, 4u);
        if (value > 65535u) return 0;
        memcpy(&value, cp->objective_data + i * 16u + 12u, 4u);
        if (value > 3u) return 0;
    }
    return 1;
}

/* The one load the file is restored for: the title's Continue Mission item
 * or F9 / Back + Y name the mission; the next load hook takes it whatever it
 * loads, so a different mission or the title's own start never restores. */
static struct { int armed; uint32_t level, difficulty; } black_cp_armed;

static void black_cp_arm(uint32_t level, uint32_t difficulty)
{
    black_cp_armed.level = level;
    black_cp_armed.difficulty = difficulty;
    black_cp_armed.armed = 1;
}

static int black_cp_armed_for(const BlackCheckpointContext *c)
{
    return black_cp_armed.armed && black_cp_armed.level == black_cp_memory(c->core)[0x5AECu] &&
        black_cp_armed.difficulty == black_cp_r32(c->main + 0x2014Cu);
}

/* This mission's file, whole and valid for the loaded mission; 1 when it is. */
static int black_cp_check(const BlackCheckpointContext *c, BlackCheckpoint *cp)
{
    char path[BLACK_CP_PATH_MAX];
    return black_cp_path(c, path, sizeof path) && black_cp_read(path, cp) && black_cp_valid(cp, c);
}

/* Runs after the mission's tables exist and before its first streaming unit
 * reads the unit fields, so the title streams the checkpoint unit itself.
 * Everything is validated first; only current allocations are written. */
static void black_cp_load(uint32_t core)
{
    BlackCheckpointContext c;
    BlackCheckpoint *cp;
    uint32_t parity;
    const int trace = getenv("RECOMP_BLACK_CHECKPOINT_TRACE") != NULL;
    const uint8_t live = black_cp_memory(0x2F3BF8u)[0];
    const int context = !live && black_cp_context(&c, 1) && core == c.core;
    const int asked = context && black_cp_armed_for(&c);
    black_cp_armed.armed = 0;
    /* A live checkpoint is already authoritative, including after death. */
    if (!context || !asked) {
        if (trace) fprintf(stderr, "[CHECKPOINT_TRACE] load skipped: live=%u context=%d asked=%d\n",
            live, context, asked);
        return;
    }
    cp = (BlackCheckpoint *)malloc(sizeof(*cp));
    if (!cp) return;
    if (!black_cp_check(&c, cp)) {
        char path[BLACK_CP_PATH_MAX];
        black_cp_path(&c, path, sizeof path);
        fprintf(stderr, "[CHECKPOINT] no valid checkpoint to restore (%s, memory stamp %08X)\n",
            path, black_cp_r32(c.profile));
        free(cp);
        return;
    }
    memcpy(black_cp_memory(c.core + 0x3F0u), cp->player, 80);
    memcpy(black_cp_memory(c.inventory), cp->inventory, c.gun_count);
    {
        uint32_t i;
        for (i = 0; i < c.objective_count; ++i) {
            const uint32_t entry = c.objective_backup + i * 12u;
            black_cp_w32(entry, black_cp_r32(c.objective_data + i * 12u));
            memcpy(black_cp_memory(entry + 4u), cp->objective_data + i * 16u + 8u, 8u);
        }
    }
    memcpy(black_cp_memory(0x2F3BF4u), cp->stats, 4);
    memcpy(black_cp_memory(0x2F3C00u), cp->stats + 4, 16);
    black_cp_w32(0x2A7EDCu, cp->unit);
    black_cp_w32(0x2EC464u, 1);
    black_cp_w32(c.core + 0x5AF0u, cp->unit);
    black_cp_memory(c.main)[0x2020Du] = (uint8_t)cp->unit;
    parity = (cp->unit + !(black_cp_memory(c.header)[0x12] & 1u)) % 2u;
    black_cp_w32(c.core + 0x49D4u + parity * 0x880u, cp->unit);
    black_cp_w32(c.core + 0x49D4u + (parity ^ 1u) * 0x880u,
        cp->unit < black_cp_memory(c.header)[0x11] ? cp->unit + 1u : UINT32_MAX);
    black_cp_memory(c.core)[0x5AEEu] = (uint8_t)(cp->unit < black_cp_memory(c.header)[0x11] ? parity : parity ^ 1u);
    black_cp_memory(0x2F3BF8u)[0] = 1;
    fprintf(stderr, "[CHECKPOINT] restored level=%u stage=%u difficulty=%u unit=%u\n",
        cp->level, cp->stage, cp->difficulty, cp->unit);
    free(cp);
}

/* A restart from the checkpoint, in two steps: F9 or Back + Y only ask; the
 * game's own Flash tick checks the run (a mission whose run has a checkpoint,
 * the live flag, and whose file is whole and valid) and has the panel ask the
 * player; a confirmed restart is checked again, arms the load and runs the
 * pause menu's own restart (RestartLevel). Anything else would restart from
 * the mission's start and lose the run. */
enum { BLACK_CP_RESTART_NONE, BLACK_CP_RESTART_ASK, BLACK_CP_RESTART_GO };
static atomic_int black_cp_restart_request, black_cp_restart_confirmed;

static void black_cp_restart_ask(void)
{
    atomic_store(&black_cp_restart_request, 1);
}

static void black_cp_restart_confirm(void)
{
    atomic_store(&black_cp_restart_confirmed, 1);
}

static void black_cp_restart_clear(void)
{
    atomic_store(&black_cp_restart_request, 0);
    atomic_store(&black_cp_restart_confirmed, 0);
}

static int black_cp_restart_valid(int in_mission, BlackCheckpointContext *c)
{
    BlackCheckpoint *cp;
    int valid;
    black_cp_root_ready();
    if (!in_mission || !black_cp_memory(0x2F3BF8u)[0] || !black_cp_context(c, 1)) {
        fprintf(stderr, "[CHECKPOINT] restart: this run has no checkpoint yet\n");
        return 0;
    }
    cp = (BlackCheckpoint *)malloc(sizeof(*cp));
    valid = cp && black_cp_check(c, cp);
    free(cp);
    if (!valid) fprintf(stderr, "[CHECKPOINT] restart: no valid checkpoint file for this mission\n");
    return valid;
}

/* NONE, ASK (open the confirmation) or GO (armed: run RestartLevel). */
static int black_cp_restart_take(int in_mission)
{
    BlackCheckpointContext c;
    if (atomic_exchange(&black_cp_restart_confirmed, 0)) {
        atomic_store(&black_cp_restart_request, 0);
        if (!black_cp_restart_valid(in_mission, &c)) return BLACK_CP_RESTART_NONE;
        black_cp_arm(black_cp_memory(c.core)[0x5AECu], black_cp_r32(c.main + 0x2014Cu));
        return BLACK_CP_RESTART_GO;
    }
    if (!atomic_exchange(&black_cp_restart_request, 0)) return BLACK_CP_RESTART_NONE;
    return black_cp_restart_valid(in_mission, &c) ? BLACK_CP_RESTART_ASK : BLACK_CP_RESTART_NONE;
}

/* The title's own RestartMission (Mission Failed) and RestartLevel (pause)
 * clear the live checkpoint and set the first unit: a restart from the
 * mission's start, which its confirmation says. The file goes with it,
 * unless F9 / Back + Y armed this restart to come back to it. */
static void black_cp_native_restart(void)
{
    BlackCheckpointContext c;
    char path[BLACK_CP_PATH_MAX];
    if (!black_cp_context(&c, 0)) return;
    if (black_cp_armed_for(&c)) return;
    black_cp_armed.armed = 0;
    if (black_cp_path(&c, path, sizeof path) && unlink(path) == 0)
        fprintf(stderr, "[CHECKPOINT] discarded: the mission restarts from its start\n");
}

/* The save file's slot identity: its stamp at +0xC8 and the player name at
 * +0xD0 (the same 80 bytes the record carries). Every slot of one profile
 * has the same name, so the stamp tells the slots apart. */
static const uint8_t *black_cp_slot_name(const uint8_t *profile, int slot)
{
    if (!profile || slot < 0 || slot >= BLACK_PROFILE_SLOTS) return NULL;
    return profile + slot * BLACK_PROFILE_SLOT + BLACK_PROFILE_NAME;
}

static uint32_t black_cp_slot_stamp(const uint8_t *profile, int slot)
{
    uint32_t stamp = 0;
    if (!profile || slot < 0 || slot >= BLACK_PROFILE_SLOTS) return 0;
    memcpy(&stamp, profile + slot * BLACK_PROFILE_SLOT + BLACK_PROFILE_STAMP, 4);
    return stamp;
}

/* Every intact record in the directory, on the panel's or the game's thread. */
static void black_cp_each(void (*visit)(const char *, BlackCheckpoint *, const struct stat *, void *), void *data)
{
    DIR *dir;
    struct dirent *entry;
    BlackCheckpoint *cp;
    black_cp_root_ready();
    if (!black_cp_root[0]) return;
    dir = opendir(black_cp_root);
    if (!dir) return;
    cp = (BlackCheckpoint *)malloc(sizeof(*cp));
    if (!cp) { closedir(dir); return; }
    while ((entry = readdir(dir))) {
        char path[BLACK_CP_PATH_MAX];
        struct stat status;
        size_t length = strlen(entry->d_name);
        if (length < 4 || strcmp(entry->d_name + length - 4, ".bin")) continue;
        snprintf(path, sizeof path, "%s/%s", black_cp_root, entry->d_name);
        if (stat(path, &status) != 0 || !S_ISREG(status.st_mode) || !black_cp_read(path, cp)) continue;
        visit(path, cp, &status, data);
    }
    free(cp);
    closedir(dir);
}

typedef struct {
    const uint8_t *name;
    uint32_t stamp;
    BlackCheckpoint *best;
    struct timespec newest;
    int found;
} BlackCpSlotScan;

static void black_cp_slot_visit(const char *path, BlackCheckpoint *cp, const struct stat *status, void *data)
{
    BlackCpSlotScan *scan = (BlackCpSlotScan *)data;
    (void)path;
    if (cp->magic != 0x50434C42u || cp->version != 2u || cp->unit < 2u ||
        cp->profile_stamp != scan->stamp || memcmp(cp->name, scan->name, 80u)) return;
    const struct timespec when = BLACK_CP_MTIME(status);
    if (scan->found && (when.tv_sec < scan->newest.tv_sec ||
        (when.tv_sec == scan->newest.tv_sec && when.tv_nsec <= scan->newest.tv_nsec))) return;
    scan->found = 1; scan->newest = when; *scan->best = *cp;
}

static int black_cp_slot_scan(const uint8_t *profile, int slot, BlackCheckpoint *best)
{
    const uint8_t *name = black_cp_slot_name(profile, slot);
    const uint32_t stamp = black_cp_slot_stamp(profile, slot);
    BlackCpSlotScan scan = {name, stamp, best, {0, 0}, 0};
    /* A slot that the game has not saved yet has no records of its own. */
    if (!name || !stamp) return 0;
    black_cp_each(black_cp_slot_visit, &scan);
    return scan.found;
}

int black_CheckpointForSlot(const uint8_t *profile, int slot, BlackCheckpointInfo *out)
{
    BlackCheckpoint cp;
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !black_cp_slot_scan(profile, slot, &cp)) return 0;
    out->difficulty = cp.difficulty;
    out->level = cp.level;
    out->stage = cp.stage;
    out->unit = cp.unit;
    return 1;
}

/* From the file names, so an unreadable record of this slot goes too: the
 * stamp and name hash identify the slot, and the rest changes per capture. */
int black_CheckpointRemoveSlot(const uint8_t *profile, int slot)
{
    const uint8_t *name = black_cp_slot_name(profile, slot);
    const uint32_t stamp = black_cp_slot_stamp(profile, slot);
    uint32_t hash;
    DIR *dir;
    struct dirent *entry;
    int removed = 0;
    if (!name || !stamp) return 0;
    hash = black_cp_hash(name, 80u);
    black_cp_root_ready();
    if (!black_cp_root[0]) return 0;
    dir = opendir(black_cp_root);
    if (!dir) return 0;
    while ((entry = readdir(dir))) {
        char path[BLACK_CP_PATH_MAX];
        unsigned file_stamp, recorded, difficulty, level, stage;
        if (sscanf(entry->d_name, "%8X-%8X-%u-%u-%u.bin",
                   &file_stamp, &recorded, &difficulty, &level, &stage) != 5 ||
            file_stamp != stamp || recorded != hash) continue;
        (void)difficulty; (void)level; (void)stage;
        snprintf(path, sizeof path, "%s/%s", black_cp_root, entry->d_name);
        if (unlink(path) == 0) ++removed;
    }
    closedir(dir);
    return removed;
}

/* The title's checkpoint item: the active save slot's newest record, valid
 * for this profile (the same identity the load hook requires) and for a
 * mission the title has unlocked (the check the title's start action makes).
 * The unlock byte sits at +0x1A of each 32-byte campaign record. */
static int black_cp_resume_unlocked(uint32_t level, uint32_t difficulty)
{
    int index = -1, i;
    uint32_t at;
    if (difficulty > 3u || level > 8u || !black_cp_ram_span(0x288648u, 8u)) return 0;
    for (i = 0; i < 8; ++i)
        if ((uint8_t)black_cp_memory(0x288648u)[i] == (uint8_t)level) { index = i; break; }
    if (index < 0) return 0;
    at = 0x2D0D9Eu + ((uint32_t)(index + (int)difficulty * 8) << 5);
    return black_cp_ram_span(at, 1u) && black_cp_memory(at)[0] == 1u;
}

static int black_cp_resume_find(BlackCheckpoint *cp)
{
    BlackCheckpointContext c;
    uint32_t manager, slot;
    if (!black_cp_profile_context(&c)) return 0;
    manager = black_cp_r32(0x2D19BCu);
    if (!black_cp_ram_span(manager + 0x524u, 4u) ||
        !black_cp_ram_span(manager + 0x1268u, BLACK_PROFILE_SIZE)) return 0;
    slot = black_cp_r32(manager + 0x524u);
    if (slot > 2u) return 0;
    if (!black_cp_slot_scan(black_cp_memory(manager + 0x1268u), (int)slot, cp)) return 0;
    if (cp->profile_stamp != black_cp_profile_stamp(&c) ||
        cp->campaign_hash != black_cp_hash(black_cp_memory(c.profile + 92u), 1024u) ||
        memcmp(cp->name, black_cp_memory(c.profile + 8u), 80u) ||
        !cp->stage || cp->unit < 2u) return 0;
    return black_cp_resume_unlocked(cp->level, cp->difficulty);
}

static int black_cp_resume_offer(void)
{
    BlackCheckpoint cp;
    return black_cp_resume_find(&cp);
}

/* The title's start action (0x124CF0) reads these fields and then asks the
 * front end to leave for FMVPlayer; the live flag would make the load hook
 * keep a checkpoint the previous mission captured instead of this file. */
static int black_cp_resume_prepare(void)
{
    BlackCheckpoint cp;
    uint32_t main;
    if (!black_cp_resume_find(&cp)) return 0;
    main = black_cp_r32(0x2D45F0u);
    if (!black_cp_ram_span(main, 0x21090u)) return 0;
    black_cp_memory(main)[0x2020Cu] = (uint8_t)cp.level;
    black_cp_memory(main)[0x2020Du] = (uint8_t)cp.unit;
    black_cp_memory(main)[0x2020Eu] = (uint8_t)cp.stage;
    black_cp_w32(main + 0x2014Cu, cp.difficulty);
    black_cp_memory(0x2F3BF8u)[0] = 0;
    black_cp_arm(cp.level, cp.difficulty);
    fprintf(stderr, "[CHECKPOINT] title resume level=%u stage=%u difficulty=%u unit=%u\n",
        cp.level, cp.stage, cp.difficulty, cp.unit);
    return 1;
}

/* A successful profile autosave gives the on-disk profile a new date. Store
 * the surviving checkpoints under that date too, so the next launch finds
 * them, and retire the ones whose campaign results the save replaced. The
 * running title keeps the old date in memory, so its own files stay; only
 * that date's records belong to the slot being saved. */
typedef struct {
    const uint8_t *name;
    uint32_t memory_stamp, disk_stamp, saved_stamp, campaign;
} BlackCpRebind;

static void black_cp_rebind_visit(const char *path, BlackCheckpoint *cp, const struct stat *status, void *data)
{
    BlackCpRebind *rebind = (BlackCpRebind *)data;
    char copy[BLACK_CP_PATH_MAX];
    if (cp->profile_stamp != rebind->disk_stamp ||
        memcmp(cp->name, rebind->name, 80u)) return;
    if (cp->campaign_hash != rebind->campaign) { unlink(path); return; }
    cp->profile_stamp = rebind->saved_stamp;
    black_cp_record_path(cp, copy, sizeof copy);
    if (!black_cp_write(copy, cp, status)) {
        fprintf(stderr, "[CHECKPOINT] profile-save rebinding failed\n");
        return;
    }
    /* The menu and the next launch look under the new date, and this launch
     * reaches the copy through the alias (black_cp_disk_stamp); a copy left
     * under the old date would hide from the slot's delete. */
    if (strcmp(path, copy)) unlink(path);
}

/* Every successful save, whatever the Saves page says: the slot's record for
 * the Saves page (slot_info.h). Only checkpoint files wait for its setting. */
#ifndef BLACK_CP_SLOT_SAVED
#define BLACK_CP_SLOT_SAVED(slot, bytes, previous_stamp) ((void)0)
#endif
#ifndef BLACK_CP_FILES_ON
#define BLACK_CP_FILES_ON() 1
#endif

static void black_cp_profile_saved(uint32_t controller)
{
    BlackCpRebind rebind;
    uint32_t manager, profile, slot, payload;
    if (!black_cp_ram_span(controller, 0x2A68u)) return;
    manager = black_cp_r32(0x2D19B4u);
    if (!black_cp_ram_span(manager, 4u)) return;
    profile = black_cp_r32(manager);
    if (!black_cp_ram_span(profile, 0x6A8u)) return;
    slot = black_cp_r32(controller + 1316u);
    if (slot > 2u) return;
    payload = controller + 4712u + slot * 1912u;
    rebind.name = black_cp_memory(profile + 8u);
    rebind.memory_stamp = black_cp_r32(profile);
    rebind.disk_stamp = black_cp_disk_stamp(rebind.memory_stamp, rebind.name);
    rebind.saved_stamp = black_cp_r32(payload + 0xC8u);
    rebind.campaign = black_cp_hash(black_cp_memory(payload + 0x124u), 1024u);
    if (black_cp_r32(payload) != 38u || black_cp_memory(payload)[0x76Du] || !rebind.saved_stamp ||
        memcmp(rebind.name, black_cp_memory(payload + 0xD0u), 80u)) return;
    BLACK_CP_SLOT_SAVED((int)slot, black_cp_memory(payload), rebind.disk_stamp);
    if (!rebind.memory_stamp || rebind.disk_stamp == rebind.saved_stamp) return;
    if (BLACK_CP_FILES_ON()) black_cp_each(black_cp_rebind_visit, &rebind);
    if (rebind.memory_stamp != rebind.saved_stamp)
        black_cp_alias_set(rebind.memory_stamp, rebind.saved_stamp, rebind.name);
}

/* All hooks are game-thread calls. Preserve the caller's complete FP controls
 * and sticky flags across host allocation, filesystem and diagnostic code. */
static void black_cp_event(uint32_t event, uint32_t core)
{
    fenv_t environment;
#if defined(__x86_64__)
    const unsigned csr = _mm_getcsr();
#endif
    fegetenv(&environment);
#if defined(__x86_64__)
    _mm_setcsr(0x1F80u);
#endif
    fesetenv(FE_DFL_ENV);
    black_cp_root_ready();
    if (event == 1u) black_cp_save();
    else if (event == 2u) black_cp_load(core);
    else if (event == 3u) black_cp_native_restart();
    else if (event == 4u) black_cp_profile_saved(core);
    fesetenv(&environment);
#if defined(__x86_64__)
    _mm_setcsr(csr);
#endif
}
