#ifndef BLACK_FRAME_STATS_H
#define BLACK_FRAME_STATS_H
#include <stdint.h>
/* The performance overlay's numbers, sampled from the guest's world timer and
 * from the presenter, over the last few seconds. Not a trace: no files. */
enum { BLACK_PERF_OFF, BLACK_PERF_FPS, BLACK_PERF_FRAMES };
typedef struct {
    int samples;          /* world intervals in the window; 0 until two frames */
    double fps;           /* 1000 / mean interval */
    double average, p50, p95, max; /* milliseconds */
    int display_samples;
    double display_fps;
} BlackFrameStats;
/* now_ns: a monotonic clock in nanoseconds. black_FrameStatsNow is the real
 * one, independent of graphics tracing; a test feeds exact intervals. */
uint64_t black_FrameStatsNow(void);
void black_FrameStatsWorld(uint32_t frame, uint64_t now_ns);
void black_FrameStatsPresent(uint32_t frame, uint64_t now_ns);
void black_FrameStatsSnapshot(BlackFrameStats *stats);
void black_FrameStatsReset(void);
#endif
