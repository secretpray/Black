/* The game window's size, place and full screen, kept between launches in
 * the port's settings (port_settings.c: window_frame, window_full_screen). */
#ifndef BLACK_WINDOW_STATE_H
#define BLACK_WINDOW_STATE_H
#ifdef __OBJC__
@class NSWindow;
/* After the presenter makes its window: the saved frame, then full screen.
 * RECOMP_FULLSCREEN=1 or 0 chooses for this run (black.py run); unset (the
 * app) the saved choice, full screen until one is saved. RECOMP_WINDOW (a
 * height) keeps the presenter's own size. */
void black_WindowRestore(NSWindow *window);
#endif
/* Main thread: writes a changed frame half a second after the last move
 * (black_MenuPump), and at once before quitting (black_HostQuit). */
void black_WindowStatePump(void);
void black_WindowStateFlush(void);
#endif
