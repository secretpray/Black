/* Black Box: the last minute of guest-visible pad input, plus enough of the
 * run to describe it. The ring is always recording (a press just saves it);
 * BLACK_BLACK_BOX_DIR also streams the whole run to a file, and
 * BLACK_INPUT_REPLAY feeds a saved file back through xinput_device.c. */
#include "black_box.h"
#include "port_menu.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 60 s of 120 Hz polls, with room for jitter before the oldest is dropped. */
#define BOX_WINDOW_MS 60000u
#define BOX_CAPACITY 8192
/* An unchanged state is still written now and then: the file then shows the
 * poll cadence and a hold that lasts. */
#define BOX_HEARTBEAT_MS 250u
/* The guest polls once a frame (30 Hz), so every poll is far below the
 * heartbeat: it must be measured from the last written sample, not the last
 * poll, or a hold would leave a single row. */

typedef struct {
    uint32_t t;          /* ms since the first poll */
    uint16_t buttons;
    uint8_t analog[8];   /* A, B, X, Y, Black, White, LTrig, RTrig */
    int16_t axes[4];     /* LX, LY, RX, RY */
} BoxSample;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static BoxSample ring[BOX_CAPACITY];
static unsigned ring_count, ring_next;
static uint32_t start_ms;
static int started;
static BoxSample last, kept;
static int have_kept;

static char saved_path[1024];
static FILE *session;
static int session_tried;

static BoxSample *replay;
static size_t replay_count, replay_at;
static int replay_loaded;
static uint32_t replay_start_ms;
static int replay_announced;

static uint32_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u);
}

static void stamp(char *out, size_t size, const char *suffix)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    snprintf(out, size, "%04d%02d%02d-%02d%02d%02d%s", local.tm_year + 1900,
        local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, suffix);
}

/* The settings and the launch options a bug report needs; both are short. */
static void write_header(FILE *file, uint32_t recorded_ms)
{
    char when[32];
    stamp(when, sizeof when, "");
    fprintf(file, "# BLACK input box\n# recorded %s, %.0f s from the first poll\n", when, recorded_ms / 1000.0);
    fprintf(file, "# t_ms buttons a b x y black white lt rt lx ly rx ry\n");
    const char *settings = black_SettingsPath();
    FILE *ini = settings ? fopen(settings, "r") : NULL;
    if (ini) {
        char line[512];
        fprintf(file, "# settings %s\n", settings);
        while (fgets(line, sizeof line, ini)) {
            line[strcspn(line, "\r\n")] = 0;
            if (*line) fprintf(file, "# %s\n", line);
        }
        fclose(ini);
    }
    static const char *keys[] = {
        "RECOMP_SCALE", "RECOMP_AA", "RECOMP_AF", "RECOMP_UPSCALE", "RECOMP_FOV",
        "BLACK_CONSOLE_GAME_CLOCK", "BLACK_REALTIME_GAME_CLOCK", "BLACK_ORIGINAL_GAME_CLOCK",
        "BLACK_GAMMA", "BLACK_LEGACY_PACING"
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        const char *value = getenv(keys[i]);
        if (value && *value) fprintf(file, "# env %s=%s\n", keys[i], value);
    }
}

static void write_sample(FILE *file, const BoxSample *sample)
{
    fprintf(file, "%u 0x%04X %u %u %u %u %u %u %u %u %d %d %d %d\n",
        sample->t, sample->buttons,
        sample->analog[0], sample->analog[1], sample->analog[2], sample->analog[3],
        sample->analog[4], sample->analog[5], sample->analog[6], sample->analog[7],
        sample->axes[0], sample->axes[1], sample->axes[2], sample->axes[3]);
}

/* Caller holds lock. */
static void open_session(uint32_t recorded_ms)
{
    session_tried = 1;
    const char *directory = getenv("BLACK_BLACK_BOX_DIR");
    if (!directory || !*directory) return;
    char name[64], path[1200];
    /* Not the saved window's name: pressing Save must not replace the run. */
    stamp(name, sizeof name, "-run.csv");
    snprintf(path, sizeof path, "%s/black-box-%s", directory, name);
    session = fopen(path, "w");
    if (!session) {
        fprintf(stderr, "[BLACK-BOX] could not write %s\n", path);
        return;
    }
    write_header(session, recorded_ms);
    fflush(session);
    fprintf(stderr, "[BLACK-BOX] recording this run to %s\n", path);
}

void black_BoxRecord(uint16_t buttons, const uint8_t analog[8], const int16_t axes[4])
{
    uint32_t now = monotonic_ms();
    BoxSample sample = {.buttons = buttons};
    memcpy(sample.analog, analog, sizeof sample.analog);
    memcpy(sample.axes, axes, sizeof sample.axes);
    pthread_mutex_lock(&lock);
    if (!started) {
        started = 1;
        start_ms = now;
    }
    sample.t = now - start_ms;
    if (session_tried == 0) open_session(sample.t);
    int changed = ring_count && (last.buttons != buttons ||
        memcmp(last.analog, analog, 8) || memcmp(last.axes, axes, sizeof last.axes));
    int beat = have_kept && sample.t - kept.t >= BOX_HEARTBEAT_MS;
    if (!have_kept || changed || beat) {
        ring[ring_next] = sample;
        ring_next = (ring_next + 1) % BOX_CAPACITY;
        if (ring_count < BOX_CAPACITY) ++ring_count;
        if (session) write_sample(session, &sample), fflush(session);
        kept = sample;
        have_kept = 1;
    }
    last = sample;
    pthread_mutex_unlock(&lock);
}

int black_BoxSave(void)
{
    BoxSample *copy = NULL;
    unsigned count = 0, start = 0;
    pthread_mutex_lock(&lock);
    if (ring_count) {
        count = ring_count;
        start = ring_count < BOX_CAPACITY ? 0 : ring_next;
        copy = malloc(count * sizeof *copy);
        if (copy)
            for (unsigned i = 0; i < count; ++i)
                copy[i] = ring[(start + i) % BOX_CAPACITY];
    }
    uint32_t window = ring_count ? last.t : 0;
    pthread_mutex_unlock(&lock);
    if (!copy) return 0;
    /* Capacity holds a minute at the usual poll rate; a slower guest could
     * keep older state, so save only the last minute by time as well. */
    unsigned first = 0;
    while (first < count && window - copy[first].t > BOX_WINDOW_MS) ++first;
    if (first) {
        count -= first;
        memmove(copy, copy + first, count * sizeof *copy);
    }

    char directory[1024] = ".", name[64], path[1200];
    const char *settings = black_SettingsPath();
    if (settings && *settings) {
        const char *slash = strrchr(settings, '/');
        if (slash && (size_t)(slash - settings) < sizeof directory) {
            memcpy(directory, settings, (size_t)(slash - settings));
            directory[slash - settings] = 0;
        }
    }
    stamp(name, sizeof name, ".csv");
    snprintf(path, sizeof path, "%s/black-box-%s", directory, name);
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "[BLACK-BOX] could not write %s\n", path);
        free(copy);
        return 0;
    }
    write_header(file, window);
    for (unsigned i = 0; i < count; ++i) write_sample(file, &copy[i]);
    int ok = fflush(file) == 0 && fclose(file) == 0;
    free(copy);
    if (!ok) return 0;
    snprintf(saved_path, sizeof saved_path, "%s", path);
    fprintf(stderr, "[BLACK-BOX] saved %u samples to %s\n", count, path);
    return 1;
}

const char *black_BoxPath(void)
{
    return saved_path;
}

/* Caller holds lock. */
static void load_replay(void)
{
    replay_loaded = 1;
    const char *path = getenv("BLACK_INPUT_REPLAY");
    if (!path || !*path) return;
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "[BLACK-BOX] could not read %s\n", path);
        return;
    }
    size_t capacity = 4096;
    BoxSample *list = malloc(capacity * sizeof *list);
    char line[256];
    while (list && fgets(line, sizeof line, file)) {
        if (line[0] == '#') continue;
        unsigned t, buttons, analog[8];
        int axes[4];
        if (sscanf(line, "%u %x %u %u %u %u %u %u %u %u %d %d %d %d",
                &t, &buttons, &analog[0], &analog[1], &analog[2], &analog[3],
                &analog[4], &analog[5], &analog[6], &analog[7],
                &axes[0], &axes[1], &axes[2], &axes[3]) != 14) continue;
        if (replay_count == capacity) {
            capacity *= 2;
            BoxSample *grown = realloc(list, capacity * sizeof *list);
            if (!grown) break;
            list = grown;
        }
        BoxSample *sample = &list[replay_count++];
        sample->t = t;
        sample->buttons = (uint16_t)buttons;
        for (int i = 0; i < 8; ++i) sample->analog[i] = (uint8_t)analog[i];
        for (int i = 0; i < 4; ++i) sample->axes[i] = (int16_t)axes[i];
    }
    fclose(file);
    replay = list;
    fprintf(stderr, "[BLACK-BOX] replaying %zu samples from %s\n", replay_count, path);
}

int black_BoxReplay(uint16_t *buttons, uint8_t analog[8], int16_t axes[4])
{
    if (!buttons || !analog || !axes) return 0;
    pthread_mutex_lock(&lock);
    if (!replay_loaded) load_replay();
    if (!replay_count) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    uint32_t now = monotonic_ms();
    if (!replay_start_ms) replay_start_ms = now;
    /* The saved window may begin after the run did (a pruned old head), so the
     * replay clock starts at the first sample's own time. */
    uint32_t elapsed = replay[0].t + (now - replay_start_ms);
    while (replay_at + 1 < replay_count && replay[replay_at + 1].t <= elapsed) ++replay_at;
    const BoxSample *sample = &replay[replay_at];
    *buttons = sample->buttons;
    memcpy(analog, sample->analog, 8);
    memcpy(axes, sample->axes, sizeof sample->axes);
    if (!replay_announced) {
        replay_announced = 1;
        fprintf(stderr, "[BLACK-BOX] replay starts (%zu samples)\n", replay_count);
    }
    pthread_mutex_unlock(&lock);
    return 1;
}
