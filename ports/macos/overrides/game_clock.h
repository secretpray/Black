#ifndef BLACK_GAME_CLOCK_H
#define BLACK_GAME_CLOCK_H
#include <stdint.h>
#include <math.h>
typedef struct {
    uint32_t frame;
    uint64_t time_ns;
    float delta;
    int initialized;
} BlackGameClock;

typedef struct {
    uint32_t frame;
    uint64_t deadline_ns;
    int initialized;
} BlackConsolePacer;

/* One deadline per guest frame; no catch-up burst after a stall. The guest
 * continues using its original fixed dt, so per-frame physics stays intact. */
static inline uint64_t black_console_wait(BlackConsolePacer *p, uint32_t frame,
    uint64_t now, uint64_t period)
{
    if (!period) return 0;
    if (!p->initialized) {
        p->initialized = 1;
        p->frame = frame;
        p->deadline_ns = now;
        return 0;
    }
    if (p->frame == frame) return 0;
    p->frame = frame;
    p->deadline_ns += period;
    if (now > p->deadline_ns && now - p->deadline_ns >= period)
        p->deadline_ns = now;
    return p->deadline_ns > now ? p->deadline_ns - now : 0;
}

static inline float black_frame_delta(BlackGameClock *clock, uint32_t frame,
    uint64_t now_ns, float original)
{
    if (!(original > 0.0f) || !isfinite(original)) original = 1.0f / 30.0f;
    if (!clock->initialized) {
        clock->initialized = 1;
        clock->frame = frame;
        clock->time_ns = now_ns;
        clock->delta = original;
    } else if (frame != clock->frame) {
        double elapsed = now_ns >= clock->time_ns ?
            (now_ns - clock->time_ns) / 1000000000.0 : 0.0;
        clock->delta = (float)fmin(elapsed, original);
        clock->frame = frame;
        clock->time_ns = now_ns;
    }
    return clock->delta;
}
#endif
