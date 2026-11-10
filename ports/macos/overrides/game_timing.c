/* Native-C ABI equivalent of the timer correction in TGP's
 * src/game/hooks.cpp (sub_000BE3F0). Never link the C++ X86Context hook into
 * this runtime. The generated original is renamed only in the build tree. */
#include "recomp_types.h"
#include "game_clock.h"
#include "port_menu.h"
#include "frame_trace.h"
#include "frame_stats.h"
#include "frame_wait.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

extern void black_OriginalTimerStep(void);
int black_ModsGameSpeed(void);
static BlackGameClock clock_state;
static pthread_mutex_t clock_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local BlackConsolePacer console_pacer;
static _Thread_local uint32_t paced_world;

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
}

static double timer_steps(uint32_t timer)
{
    uint32_t divisor = MEM32(timer + 0x18);
    if (!divisor) divisor = 1;
    return MEM32(timer + 0x0C) + (double)MEM32(timer + 0x10) / divisor;
}

void sub_000BE3F0(void)
{
    uint32_t timer = g_ecx;
    uint32_t world = MEM32(0x2D45D4), game = MEM32(0x2D45F0);
    /* The overlay's history must not span a settings pause: the first frame
     * after closing would read as a several-second hitch. Restart sampling at
     * both edges, before this call becomes the new baseline. */
    static int menu_before;
    int menu = black_MenuIsOpen();
    if (menu != menu_before) {
        menu_before = menu;
        black_FrameStatsReset();
    }
    if (timer == world && world) black_FrameStatsWorld(MEM32(0x2F4368), black_FrameStatsNow());
    /* Freeze only initialized gameplay, from its own thread. Cocoa and the
     * independent USB/APU threads remain live; movie backgrounds keep playing. */
    if (timer == world && world && game &&
        MEM32(game + 0x20220 + 0x4C + 0x1A8) == 0x37 &&
        (MEMF(world + 0x130) != 0 || MEMF(world + 0x134) != 0 || MEMF(world + 0x138) != 0)) {
        while (black_MenuIsOpen()) {
            struct timespec delay = {0, 5000000};
            nanosleep(&delay, NULL);
        }
    }
    /* Direct execution must be as conservative as the launcher. Never enable
     * the partial variable-FPS port merely because an environment key is absent. */
    int console = getenv("BLACK_CONSOLE_GAME_CLOCK") != NULL;
    if ((!console && !getenv("BLACK_REALTIME_GAME_CLOCK")) || getenv("BLACK_ORIGINAL_GAME_CLOCK")) {
        black_OriginalTimerStep();
        return;
    }
    /* The Mac runtime still uses the title's XMV player; unlike TGP's full
     * hook set it must retain its original clocks. Correct only the world
     * timer, and only once a real 3D player/scene has been initialized. */
    if (!world || timer != world || !game ||
        MEM32(game + 0x20220 + 0x4C + 0x1A8) != 0x37 ||
        (MEMF(world + 0x30 + 0x100) == 0.0f && MEMF(world + 0x30 + 0x104) == 0.0f &&
         MEMF(world + 0x30 + 0x108) == 0.0f)) {
        if (timer == world) console_pacer.initialized = 0;
        black_OriginalTimerStep();
        return;
    }
    if (console) {
        uint64_t trace_start = black_trace_ns();
        if (world != paced_world) {
            console_pacer.initialized = 0;
            paced_world = world;
        }
        float step = MEMF(0x2F3A38);
        if (!(step > 0) || !isfinite(step) || step > 0.1f) step = 1.0f / 30.0f;
        uint64_t wait = black_console_wait(&console_pacer, MEM32(0x2F4368),
            monotonic_ns(), (uint64_t)llround((double)step * 1000000000.0));
        if (wait) {
            black_frame_wait(wait, getenv("BLACK_LEGACY_WORLD_WAIT") != NULL);
        }
        /* Game Speed (Mods page): the pace stays one step a frame, each step
         * takes that share of the step the game was made for, as TGP's
         * variable frame time does. */
        int speed = black_ModsGameSpeed();
        double before = timer_steps(timer);
        float before_time = MEMF(timer + 0x20);
        black_OriginalTimerStep();
        if (speed != 100) {
            uint32_t divisor = MEM32(timer + 0x18);
            if (!divisor) divisor = 1;
            float delta = step * (float)speed / 100.0f;
            MEMF(timer + 0x1C) = delta / divisor;
            MEMF(timer + 0x20) = before_time + (float)((timer_steps(timer) - before) * delta);
        }
        black_trace(BLACK_TRACE_WORLD, MEM32(0x2F4368), trace_start, black_trace_ns(), wait, 0);
        return;
    }
    double before = timer_steps(timer);
    float before_time = MEMF(timer + 0x20);
    black_OriginalTimerStep();
    uint64_t ns = monotonic_ns();
    pthread_mutex_lock(&clock_lock);
    float delta = black_frame_delta(&clock_state, MEM32(0x2F4368), ns, MEMF(0x2F3A38));
    pthread_mutex_unlock(&clock_lock);
    uint32_t divisor = MEM32(timer + 0x18);
    if (!divisor) divisor = 1;
    MEMF(timer + 0x1C) = delta / divisor;
    MEMF(timer + 0x20) = before_time + (float)((timer_steps(timer) - before) * delta);
    if (getenv("BLACK_STATE_LOG")) {
        static _Thread_local uint64_t last_report;
        if (ns - last_report >= 1000000000ull) {
            last_report = ns;
            fprintf(stderr, "[GAMECLOCK] frame=%u timer=%08X dt=%.6f original=%.6f divisor=%u\n",
                MEM32(0x2F4368), timer, delta, MEMF(0x2F3A38), divisor);
        }
    }
}
