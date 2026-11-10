/* Frame intervals for the performance overlay. The world timer and the
 * presenter run on different threads, so each keeps its own ring; a snapshot
 * copies and sorts them, which is cheap at the panel's refresh rate. */
#include "frame_stats.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLACK_FRAME_WINDOW 240
/* A pause menu, a film or a window drag can stop one of the clocks for
 * seconds. That one interval is a discontinuity, not a frame time. */
#define BLACK_FRAME_GAP_MS 1000.0

typedef struct {
    pthread_mutex_t lock;
    double ms[BLACK_FRAME_WINDOW];
    unsigned count, next;
    uint32_t frame;
    uint64_t last_ns, last_sample_ns;
    int started;
} BlackFrameRing;

static BlackFrameRing world_ring = {PTHREAD_MUTEX_INITIALIZER};
static BlackFrameRing present_ring = {PTHREAD_MUTEX_INITIALIZER};

uint64_t black_FrameStatsNow(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void record(BlackFrameRing *ring, uint32_t frame, uint64_t now)
{
    pthread_mutex_lock(&ring->lock);
    if (ring->started && frame == ring->frame) {
        pthread_mutex_unlock(&ring->lock);
        return;
    }
    int restarted = 0;
    if (ring->started && frame < ring->frame) {
        /* A new world or a counter reset: old intervals describe another scene,
         * and the interval across the change is not a frame time either. */
        ring->count = ring->next = 0;
        restarted = 1;
    }
    if (ring->started && !restarted) {
        double ms = (double)(now - ring->last_ns) / 1e6;
        if (ms > 0.1 && ms <= BLACK_FRAME_GAP_MS) {
            ring->ms[ring->next] = ms;
            ring->next = (ring->next + 1) % BLACK_FRAME_WINDOW;
            if (ring->count < BLACK_FRAME_WINDOW) ++ring->count;
            ring->last_sample_ns = now;
        }
    }
    ring->frame = frame;
    ring->last_ns = now;
    ring->started = 1;
    pthread_mutex_unlock(&ring->lock);
}

static int compare_ms(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* Copies the ring oldest first and fills fps, mean and nearest-rank
 * percentiles. Returns the sample count. */
static int summarise(BlackFrameRing *ring, double *fps, double *average,
    double *p50, double *p95, double *max)
{
    double sorted[BLACK_FRAME_WINDOW];
    unsigned count, start;
    pthread_mutex_lock(&ring->lock);
    count = ring->count;
    start = count < BLACK_FRAME_WINDOW ? 0 : ring->next;
    for (unsigned i = 0; i < count; ++i)
        sorted[i] = ring->ms[(start + i) % BLACK_FRAME_WINDOW];
    pthread_mutex_unlock(&ring->lock);
    if (!count) return 0;
    double sum = 0;
    for (unsigned i = 0; i < count; ++i) sum += sorted[i];
    qsort(sorted, count, sizeof sorted[0], compare_ms);
    *average = sum / count;
    *fps = *average > 0 ? 1000.0 / *average : 0;
    *p50 = sorted[(count - 1) / 2];
    *p95 = sorted[(count * 95 + 99) / 100 - 1];
    *max = sorted[count - 1];
    return (int)count;
}

void black_FrameStatsWorld(uint32_t frame, uint64_t now_ns)
{
    record(&world_ring, frame, now_ns);
}

void black_FrameStatsPresent(uint32_t frame, uint64_t now_ns)
{
    record(&present_ring, frame, now_ns);
}

void black_FrameStatsSnapshot(BlackFrameStats *stats)
{
    double fps, average, p50, p95, max;
    if (!stats) return;
    memset(stats, 0, sizeof *stats);
    stats->samples = summarise(&world_ring, &stats->fps, &stats->average,
        &stats->p50, &stats->p95, &stats->max);
    stats->display_samples = summarise(&present_ring, &stats->display_fps,
        &average, &p50, &p95, &max);
    /* The first interval needs two frames; until then the mean is meaningless. */
    if (stats->samples < 2) stats->fps = stats->average = 0;
}

void black_FrameStatsReset(void)
{
    pthread_mutex_lock(&world_ring.lock);
    world_ring.count = world_ring.next = 0;
    world_ring.started = 0;
    pthread_mutex_unlock(&world_ring.lock);
    pthread_mutex_lock(&present_ring.lock);
    present_ring.count = present_ring.next = 0;
    present_ring.started = 0;
    pthread_mutex_unlock(&present_ring.lock);
}
