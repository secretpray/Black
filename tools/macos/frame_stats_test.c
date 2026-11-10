/* Frame intervals the performance overlay shows: mean, median, 95th
 * percentile and worst, with a gap treated as a discontinuity. */
#undef NDEBUG
#include "frame_stats.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int close_to(double value, double expected, double tolerance)
{
    return fabs(value - expected) <= tolerance;
}

int main(void)
{
    BlackFrameStats stats;
    const uint64_t ms = 1000000ull;

    black_FrameStatsReset();
    black_FrameStatsSnapshot(&stats);
    assert(stats.samples == 0 && stats.display_samples == 0);

    /* 227 fast frames and 13 slow ones: the slow 5.4% is above the 95th
     * percentile, so p95 lands on the slow value and the median does not. */
    uint64_t t = 0;
    black_FrameStatsWorld(1, t);
    for (uint32_t frame = 2; frame <= 241; ++frame) {
        t += (frame > 228 ? 100 : 10) * ms;
        black_FrameStatsWorld(frame, t);
    }
    black_FrameStatsSnapshot(&stats);
    assert(stats.samples == 240);
    if (!close_to(stats.average, 14.875, 0.001)) return 1;
    if (!close_to(stats.fps, 1000.0 / 14.875, 0.001)) return 2;
    if (!close_to(stats.p50, 10.0, 0.001)) return 3;
    if (!close_to(stats.p95, 100.0, 0.001)) return 4;
    if (!close_to(stats.max, 100.0, 0.001)) return 5;

    /* Two calls in one frame are one interval, and a world reset starts over. */
    black_FrameStatsWorld(242, t += 10 * ms);
    black_FrameStatsWorld(242, t += 10 * ms);
    black_FrameStatsSnapshot(&stats);
    if (stats.samples != 240 || stats.p50 != 10.0) return 6;
    black_FrameStatsWorld(1, t += 10 * ms);
    black_FrameStatsSnapshot(&stats);
    if (stats.samples != 0) return 7;

    /* A pause leaves a gap too long to be a frame time. */
    black_FrameStatsWorld(2, t += 5000 * ms);
    black_FrameStatsWorld(3, t += 10 * ms);
    black_FrameStatsSnapshot(&stats);
    if (stats.samples != 1 || stats.p50 != 10.0) return 8;

    /* The presenter is sampled the same way at 60 Hz. */
    black_FrameStatsPresent(1, t);
    for (uint32_t frame = 2; frame <= 61; ++frame) {
        t += 16666667ull;
        black_FrameStatsPresent(frame, t);
    }
    black_FrameStatsSnapshot(&stats);
    if (stats.display_samples != 60 || !close_to(stats.display_fps, 60.0, 0.01)) return 9;

    /* Old intervals leave the window. */
    for (uint32_t frame = 62; frame <= 400; ++frame) {
        t += 25 * ms;
        black_FrameStatsPresent(frame, t);
    }
    black_FrameStatsSnapshot(&stats);
    if (stats.display_samples != 240 || !close_to(stats.display_fps, 40.0, 0.01)) return 10;
    printf("frame stats: mean %.3f ms, p50 %.1f, p95 %.1f, max %.1f; display %.1f fps\n",
        stats.average, stats.p50, stats.p95, stats.max, stats.display_fps);
    return 0;
}
