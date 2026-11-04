/* Main-thread capture transitions. Escape/focus loss can release capture
 * without being undone every frame; a deliberate device/page change resumes it. */
#pragma once
typedef struct { int keyboard, blocked, active; } BlackMouseCapture;
static inline int black_mouse_capture(BlackMouseCapture *s, int keyboard,
    int menu, int overlay, int active, int background)
{
    int blocked = menu || overlay;
    int change = -1;
    if (blocked || !keyboard || background || !active) change = 0;
    else if (s->blocked || !s->keyboard || !s->active) change = 1;
    s->keyboard = keyboard; s->blocked = blocked;
    s->active = active;
    return change;
}
