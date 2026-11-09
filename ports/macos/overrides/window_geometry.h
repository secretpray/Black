/* Where a saved game window goes back to: plain geometry, no AppKit, so it
 * is tested on its own (window_geometry_test.c). Rects are in screen points
 * with y up, as Cocoa has them; the content rect, the title bar above it. */
#ifndef BLACK_WINDOW_GEOMETRY_H
#define BLACK_WINDOW_GEOMETRY_H

typedef struct { double x, y, w, h; } BlackRect;

static inline double black_rect_overlap(BlackRect a, BlackRect b)
{
    double w = (a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w) - (a.x > b.x ? a.x : b.x);
    double h = (a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h) - (a.y > b.y ? a.y : b.y);
    return w > 0 && h > 0 ? w * h : 0;
}

/* The saved content rect fitted to today's screens (their visible frames):
 * the screen it overlaps most, or none when it overlaps none (a monitor gone:
 * return 0, the window stays centred). Kept to the game's aspect, at least
 * `min_h` tall, no larger than that screen, and moved inside it with its
 * title bar (`title` points) visible. */
static inline int black_window_fit(BlackRect saved, double aspect, double min_h, double title,
                                   const BlackRect *screens, int count, BlackRect *out)
{
    if (!(saved.w > 0) || !(saved.h > 0) || !(aspect > 0) || count <= 0) return 0;
    int best = -1; double most = 0;
    for (int i = 0; i < count; ++i) {
        double o = black_rect_overlap(saved, screens[i]);
        if (o > most) { most = o; best = i; }
    }
    if (best < 0) return 0;
    BlackRect s = screens[best];
    double h = saved.h, max_h = s.h - title, max_w = s.w;
    if (h > max_h) h = max_h;
    if (h * aspect > max_w) h = max_w / aspect;
    if (h < min_h) h = min_h;
    double w = h * aspect;
    double x = saved.x, y = saved.y + saved.h - h; /* keep the top edge where it was */
    if (x + w > s.x + s.w) x = s.x + s.w - w;
    if (x < s.x) x = s.x;
    if (y + h + title > s.y + s.h) y = s.y + s.h - title - h;
    if (y < s.y) y = s.y;
    *out = (BlackRect){x, y, w, h};
    return 1;
}
#endif
