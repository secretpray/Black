/* Main-thread capture transitions. Escape/focus loss can release capture
 * without being undone every frame; a deliberate device/page change resumes it. */
#pragma once
typedef struct { int keyboard, blocked; } BlackMouseCapture;
static inline int black_mouse_capture(BlackMouseCapture *s, int keyboard,
    int menu, int overlay, int active, int background)
{
    int blocked = menu || overlay;
    int change = -1;
    if (blocked || !keyboard || background) change = 0;
    else if (active && (s->blocked || !s->keyboard)) change = 1;
    s->keyboard = keyboard; s->blocked = blocked;
    return change;
}
