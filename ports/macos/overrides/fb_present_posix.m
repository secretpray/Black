/* Preserve the pinned presenter, but include rendering time in its 60 Hz
 * period. Sleeping a full 16.67 ms AFTER every frame reduced presentation to
 * about 51-53 Hz even when the game produced 60 flips/s. This changes only
 * host presentation pacing, not the Xbox vblank/game clock.
 * License: same as the included runtime source. */
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "frame_trace.h"
#import <QuartzCore/QuartzCore.h>
#include "smaa_metal.h"
#include "upscale_metal.h"
static int black_SmaaWanted(void)
{
    static int wanted = -1;
    if (wanted < 0) {
        const char *aa = getenv("RECOMP_AA");
        wanted = aa && !strcmp(aa, "smaa");
    }
    return wanted;
}
static int black_UpscaleWanted(void)
{
    static int wanted = -1;
    if (wanted < 0) {
        const char *upscale = getenv("RECOMP_UPSCALE");
        wanted = upscale && !strcmp(upscale, "metalfx");
    }
    return wanted;
}
static id<MTLTexture> black_PostProcess(id<MTLCommandBuffer> command, id<MTLTexture> source,
    id<CAMetalDrawable> drawable);
static int update_pixels(void);
static void render_frame(void);
static int black_StableFrameAvailable(void);
extern unsigned xnv2a_flip_count(void) __attribute__((weak));

static int black_PresenterSleep(const struct timespec *request, struct timespec *remaining)
{
    static _Thread_local uint64_t deadline;
    static int legacy = -1;
    if (legacy < 0)
        legacy = getenv("BLACK_LEGACY_PACING") != NULL;
    if (legacy)
        return nanosleep(request, remaining);
    uint64_t period = (uint64_t)request->tv_sec * 1000000000ull + request->tv_nsec;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ns = (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
    if (!deadline)
        deadline = ns;
    deadline += period;
    if (deadline <= ns) {
        /* A slow frame must not trigger a burst of catch-up presentations. */
        if (ns - deadline >= period)
            deadline = ns;
        return 0;
    }
    struct timespec delay = {
        .tv_sec = (time_t)((deadline - ns) / 1000000000ull),
        .tv_nsec = (long)((deadline - ns) % 1000000000ull)
    };
    int result;
    do {
        result = nanosleep(&delay, &delay);
    } while (result && errno == EINTR);
    return result;
}

#define nanosleep black_PresenterSleep
#define xbox_FramebufferWindowStart black_OriginalFramebufferWindowStart
#define xbox_FramebufferWindowStop black_OriginalFramebufferWindowStop
#include BLACK_PRESENTER_SOURCE
#undef nanosleep
#undef xbox_FramebufferWindowStart
#undef xbox_FramebufferWindowStop
#include "port_menu.h"
#include "flash_menu.h"

/* SMAA on the frame as rendered, then MetalFX to the exact size of the area
 * the presentation pass fills (the same fit as its viewport), which then
 * samples it 1:1 instead of stretching. */
static id<MTLTexture> black_PostProcess(id<MTLCommandBuffer> command, id<MTLTexture> source,
    id<CAMetalDrawable> drawable)
{
    if (black_trace_enabled()) {
        /* GPU time of the whole presentation buffer: SMAA, MetalFX, the
         * scaling pass. Ends at completion, on the trace's own clock. */
        uint32_t frame = (uint32_t)s_fb_frames_presented;
        [command addCompletedHandler:^(id<MTLCommandBuffer> done) {
            double gpu = done.GPUEndTime - done.GPUStartTime;
            uint64_t now = black_trace_ns();
            if (gpu > 0 && gpu < 1) black_trace(BLACK_TRACE_GPU, frame, now - (uint64_t)(gpu * 1e9), now, 0, 0);
        }];
    }
    if (black_SmaaWanted()) source = black_SmaaApply(s_device, command, source);
    if (black_UpscaleWanted()) {
        double tw = (double)drawable.texture.width, th = (double)drawable.texture.height;
        double aspect = display_aspect(), vw = tw, vh = tw / aspect;
        if (vh > th) { vh = th; vw = th * aspect; }
        source = black_UpscaleApply(s_device, command, source, (NSUInteger)floor(vw), (NSUInteger)floor(vh));
    }
    return source;
}

/* The host pumps Cocoa itself rather than running NSApplication's main loop.
 * Explicitly exit on the main thread, flushing stdio/atexit and releasing the
 * captured pointer. Do not wait forever for a blocked guest thread. */
void black_AudioFadeOut(void);
void black_HostQuit(void)
{
    /* Before exit: ending the process mid-buffer clicked in the speakers. */
    black_AudioFadeOut();
    xek_capture_mouse(NO);
    xbox_KeyboardReleaseAll();
    xbox_MouseReleaseAll();
    fprintf(stderr, "[HOST] Quit BLACK requested; exiting normally\n");
    exit(0);
}

static int black_StableFrameAvailable(void)
{
    if (!getenv("BLACK_STABLE_FRAMES") || !xnv2a_hires_acquire || pvideo_active()) return 0;
    uint64_t seq;
    uint32_t w, h, stride;
    const uint8_t *px = xnv2a_hires_acquire(&seq, &w, &h, &stride);
    if (!px) return 0;
    xnv2a_hires_release();
    return seq && w && h;
}

static int update_pixels(void)
{
    uint64_t start = black_trace_ns();
    int result;
    if (black_StableFrameAvailable()) {
        /* The source will be the immutable completed GPU snapshot below.
         * Do not race downloads into guest RAM or select a historical buffer. */
        pthread_mutex_lock(&s_pixels_lock);
        result = ensure_pixels();
        pthread_mutex_unlock(&s_pixels_lock);
    } else result = black_OriginalUpdatePixels();
    black_trace(BLACK_TRACE_PIXELS, (uint32_t)s_fb_frames_presented, start, black_trace_ns(),
        xnv2a_flip_count ? xnv2a_flip_count() : 0, result);
    return result;
}

static void render_frame(void)
{
    uint64_t start = black_trace_ns();
    uint32_t before = xnv2a_flip_count ? xnv2a_flip_count() : 0;
    black_OriginalRenderFrame();
    black_trace(BLACK_TRACE_PRESENT, (uint32_t)s_fb_frames_presented, start, black_trace_ns(),
        before, xnv2a_flip_count ? xnv2a_flip_count() : 0);
}

static void black_MenuCapture(int opening)
{
    static int restore_mouse;
    if (opening) {
        restore_mouse = s_mouse_captured;
        xbox_KeyboardReleaseAll();
        xbox_MouseReleaseAll();
        memset(s_keycode_slot, 0xFF, sizeof s_keycode_slot);
        xek_capture_mouse(NO);
    } else if (restore_mouse && NSApp.active && s_window.keyWindow) xek_capture_mouse(YES);
}

/* GPU flip paths also request lazy window startup. Suppress those in a
 * headless run, not just the main host's initial startup call. */
void xbox_FramebufferWindowStart(void)
{
    if (!getenv("BLACK_HEADLESS")) {
        black_OriginalFramebufferWindowStart();
        if ([NSThread isMainThread] && s_window) black_MenuAttach(s_window, black_MenuCapture);
    }
}

void xbox_FramebufferWindowStop(void)
{
    black_MenuDetach();
    black_OriginalFramebufferWindowStop();
}

/* Main-thread pointer mode follows the guest's actual Flash page. */
void black_HostInputPump(void)
{
    if(!s_window)return;
    static int was_menu;
    int menu=black_GameMenuActive(),active=NSApp.active&&s_window.keyWindow;
    if(!black_MenuIsOpen()){
        if(menu&&s_mouse_captured)xek_capture_mouse(NO);
        else if(!menu&&was_menu&&active&&black_InputKeyboardActive()&&!getenv("RECOMP_BACKGROUND"))xek_capture_mouse(YES);
    }
    /* Seed a newly entered menu only. Event locations are authoritative:
     * polling the global cursor here can overwrite a queued click before the
     * guest consumes it (remote/assisted pointer events need not warp it). */
    if(menu&&!was_menu){
        NSPoint p=[s_window.contentView convertPoint:s_window.mouseLocationOutsideOfEventStream fromView:nil];
        float x=0,y=0;int inside=black_FramePointer(p.x,p.y,s_window.contentView.bounds.size.width,
            s_window.contentView.bounds.size.height,(float)display_aspect(),&x,&y);
        black_GamePointerPosition(x,y,inside&&active&&!black_MenuIsOpen()&&!getenv("RECOMP_BACKGROUND"));
    }
    was_menu=menu;
}
