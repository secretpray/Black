/* See window_state.h. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "window_state.h"
#include "window_geometry.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NSWindow *watched;
static int frame_dirty, full_screen = -1, in_transition;
static double dirty_at;
static int last_saved[4];

static BOOL is_full(NSWindow *w) { return (w.styleMask & NSWindowStyleMaskFullScreen) != 0; }

static void note_frame(void)
{
    if (!watched || in_transition || is_full(watched) || watched.miniaturized) return;
    frame_dirty = 1; dirty_at = CACurrentMediaTime();
}

static void save_now(void)
{
    if (!watched) return;
    int fs = full_screen >= 0 ? full_screen : is_full(watched);
    int frame[4], *keep = NULL;
    if (frame_dirty && !is_full(watched) && !watched.miniaturized) {
        NSRect r = [watched contentRectForFrameRect:watched.frame];
        frame[0] = (int)lround(r.origin.x); frame[1] = (int)lround(r.origin.y);
        frame[2] = (int)lround(r.size.width); frame[3] = (int)lround(r.size.height);
        if (memcmp(frame, last_saved, sizeof frame)) keep = frame;
    }
    frame_dirty = 0;
    if (!keep && fs == black_SettingsWindowFullScreen()) return;
    if (black_SettingsSetWindowState(keep, fs)) {
        if (keep) memcpy(last_saved, keep, sizeof last_saved);
        if (keep) fprintf(stderr, "[WINDOW] saved frame=%d,%d,%dx%d full_screen=%d\n",
            last_saved[0], last_saved[1], last_saved[2], last_saved[3], fs);
        else fprintf(stderr, "[WINDOW] saved full_screen=%d\n", fs);
    }
}

void black_WindowStatePump(void)
{ if (frame_dirty && CACurrentMediaTime() - dirty_at > 0.5) save_now(); }

void black_WindowStateFlush(void)
{ if (frame_dirty || full_screen >= 0) save_now(); }

static void observe(NSWindow *w)
{
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    void (^frame)(NSNotification *) = ^(NSNotification *n) { (void)n; note_frame(); };
    [center addObserverForName:NSWindowDidMoveNotification object:w queue:nil usingBlock:frame];
    [center addObserverForName:NSWindowDidEndLiveResizeNotification object:w queue:nil usingBlock:frame];
    [center addObserverForName:NSWindowDidResizeNotification object:w queue:nil usingBlock:^(NSNotification *n) {
        (void)n; if (!w.inLiveResize) note_frame(); }];
    [center addObserverForName:NSWindowWillEnterFullScreenNotification object:w queue:nil usingBlock:^(NSNotification *n) {
        (void)n; if (frame_dirty) save_now(); in_transition = 1; }];
    [center addObserverForName:NSWindowDidEnterFullScreenNotification object:w queue:nil usingBlock:^(NSNotification *n) {
        (void)n; in_transition = 0; full_screen = 1; save_now(); }];
    [center addObserverForName:NSWindowWillExitFullScreenNotification object:w queue:nil usingBlock:^(NSNotification *n) {
        (void)n; in_transition = 1; }];
    [center addObserverForName:NSWindowDidExitFullScreenNotification object:w queue:nil usingBlock:^(NSNotification *n) {
        (void)n; in_transition = 0; full_screen = 0; save_now(); }];
}

void black_WindowRestore(NSWindow *window)
{
    if (!window) return;
    watched = window;
    NSWindow.allowsAutomaticWindowTabbing = NO;
    const char *choice = getenv("RECOMP_FULLSCREEN");
    int saved_fs = black_SettingsWindowFullScreen();
    int want_full = choice && *choice ? strcmp(choice, "0") != 0 : saved_fs != 0; /* -1: never saved, full */
    const char *source = choice && *choice ? "run option" : saved_fs >= 0 ? "saved" : "default";
    int frame[4];
    if (!getenv("RECOMP_WINDOW") && black_SettingsWindowFrame(frame)) {
        NSSize aspect = window.contentAspectRatio;
        double ratio = aspect.height > 0 ? aspect.width / aspect.height : (double)frame[2] / frame[3];
        BlackRect screens[16]; int count = 0;
        for (NSScreen *screen in NSScreen.screens) {
            if (count >= 16) break;
            NSRect v = screen.visibleFrame;
            screens[count++] = (BlackRect){v.origin.x, v.origin.y, v.size.width, v.size.height};
        }
        double title = window.frame.size.height - [window contentRectForFrameRect:window.frame].size.height;
        BlackRect fit;
        if (black_window_fit((BlackRect){frame[0], frame[1], frame[2], frame[3]}, ratio, window.contentMinSize.height,
                             title, screens, count, &fit)) {
            [window setFrame:[window frameRectForContentRect:NSMakeRect(fit.x, fit.y, fit.w, fit.h)] display:NO];
            memcpy(last_saved, frame, sizeof last_saved);
            fprintf(stderr, "[WINDOW] restored frame=%.0f,%.0f,%.0fx%.0f\n", fit.x, fit.y, fit.w, fit.h);
        } else fprintf(stderr, "[WINDOW] saved frame is off every screen; centred\n");
    }
    observe(window);
    fprintf(stderr, "[WINDOW] full_screen=%d (%s)\n", want_full, source);
    if (want_full && !is_full(window)) [window toggleFullScreen:nil];
}
