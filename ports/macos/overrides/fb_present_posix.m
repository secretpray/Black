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
static id<MTLTexture> black_GammaTexture(void);
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
#include "display_gamma.h"

/* The DAC table as a 256x1 lookup for the scaling pass; uploaded only when
 * the game wrote a different one. BLACK_GAMMA=off keeps identity for A/B. */
static id<MTLTexture> black_GammaTexture(void)
{
    static id<MTLTexture> texture;
    static uint32_t uploaded = UINT32_MAX;
    static int disabled = -1;
    if (disabled < 0) {
        const char *mode = getenv("BLACK_GAMMA");
        disabled = mode && !strcmp(mode, "off");
    }
    if (!texture) {
        MTLTextureDescriptor *desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:BLACK_GAMMA_ENTRIES height:1 mipmapped:NO];
        texture = [s_device newTextureWithDescriptor:desc];
    }
    uint8_t rgb[BLACK_GAMMA_ENTRIES * 3];
    uint32_t version = black_GammaSnapshot(rgb);
    if (disabled) version = 0;
    if (texture && version != uploaded) {
        uint8_t rgba[BLACK_GAMMA_ENTRIES * 4];
        for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i) {
            rgba[i * 4] = disabled ? (uint8_t)i : rgb[i * 3];
            rgba[i * 4 + 1] = disabled ? (uint8_t)i : rgb[i * 3 + 1];
            rgba[i * 4 + 2] = disabled ? (uint8_t)i : rgb[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        [texture replaceRegion:MTLRegionMake2D(0, 0, BLACK_GAMMA_ENTRIES, 1)
                   mipmapLevel:0 withBytes:rgba bytesPerRow:sizeof rgba];
        uploaded = version;
    }
    return texture;
}

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
    black_WindowStateFlush();
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
    extern void black_AudioSetMenuOpen(int);
    black_AudioSetMenuOpen(opening);
    static int restore_mouse;
    if (opening) {
        restore_mouse = s_mouse_captured;
        xbox_KeyboardReleaseAll();
        xbox_MouseReleaseAll();
        memset(s_keycode_slot, 0xFF, sizeof s_keycode_slot);
        xek_capture_mouse(NO);
    } else if (restore_mouse && black_InputKeyboardActive() && !black_GameMenuActive() &&
               NSApp.active && s_window.keyWindow) xek_capture_mouse(YES);
}

/* A click on the Dock icon sends the running app a reopen Apple event, and
 * Quit in the Dock menu, log out, restart and shut down send it a quit one.
 * The host pumps events itself and never runs -[NSApplication
 * finishLaunching], which installs AppKit's handlers, so the click did
 * nothing (Cmd+Tab, activation by the window server, worked) and the quit
 * was ignored. Reopen brings the game window back, into its full-screen
 * space if it has one; quit takes the same path as Cmd+Q. */
@interface BlackReopenHandler : NSObject
@end
@implementation BlackReopenHandler
- (void)reopen:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply
{
    (void)event; (void)reply;
    if (!s_window) return;
    if (NSApp.hidden) [NSApp unhide:nil];
    if (s_window.miniaturized) [s_window deminiaturize:nil];
    [NSApp activate];
    [s_window makeKeyAndOrderFront:nil];
}
- (void)quit:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply
{
    (void)event; (void)reply;
    void black_HostQuit(void);
    black_HostQuit();
}
@end

static void black_InstallReopenHandler(void)
{
    static BlackReopenHandler *handler;
    if (handler) return;
    /* What NSApplication's run does first, and SDL's own Cocoa loop too:
     * check in with the system as a launched app. Its default Apple event
     * handlers are replaced right after. */
    [NSApp finishLaunching];
    handler = [[BlackReopenHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:handler
        andSelector:@selector(reopen:reply:) forEventClass:'aevt' andEventID:'rapp'];
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:handler
        andSelector:@selector(quit:reply:) forEventClass:'aevt' andEventID:'quit'];
}

/* GPU flip paths also request lazy window startup. Suppress those in a
 * headless run, not just the main host's initial startup call. */
void xbox_FramebufferWindowStart(void)
{
    if (!getenv("BLACK_HEADLESS")) {
        black_OriginalFramebufferWindowStart();
        if ([NSThread isMainThread] && s_window) {
            black_MenuAttach(s_window, black_MenuCapture);
            black_InstallReopenHandler();
        }
    }
}

void xbox_FramebufferWindowStop(void)
{
    black_MenuDetach();
    black_OriginalFramebufferWindowStop();
}

#include "mouse_capture.h"
/* Main-thread pointer mode follows both the page and the selected device. */
void black_HostInputPump(void)
{
    if(!s_window)return;
    static int was_menu;
    static BlackMouseCapture capture;
    int menu=black_GameMenuActive(),active=NSApp.active&&s_window.keyWindow;
    black_InputFocusChanged(active);
    int wanted=black_mouse_capture(&capture,black_InputKeyboardActive(),menu,
        black_MenuIsOpen(),active,getenv("RECOMP_BACKGROUND")!=NULL);
    if(wanted>=0)xek_capture_mouse(wanted);
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
