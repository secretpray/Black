/* Name keyboard shortcuts on the pad: X deletes the last character, Y
 * types a space and Start finishes the name, as the page's own DELETE,
 * SPACE and DONE keys do. The game gives these buttons no job there; B
 * keeps backing out of the page. Held X repeats after a pause, like a
 * keyboard's Backspace. */
#ifndef BLACK_TEXT_PAD_H
#define BLACK_TEXT_PAD_H
#include <stdint.h>

enum { BLACK_TEXT_PAD_REPEAT_DELAY = 450, BLACK_TEXT_PAD_REPEAT_EVERY = 90 };

typedef struct { int x, y, start; uint32_t next_ms; } BlackTextPad;

/* x, y, start: buttons held now; active: the name page takes typing.
 * Returns the character to type ('\b', ' ', '\r' for DONE) or 0. Leaving
 * the page forgets held buttons, so a press made elsewhere never types on
 * arrival. */
static inline int black_text_pad(BlackTextPad *s, int active, int x, int y, int start, uint32_t now_ms)
{
    if (!active) { s->x = x; s->y = y; s->start = start; s->next_ms = 0; return 0; }
    int out = 0;
    if (start && !s->start) out = '\r';
    else if (x && !s->x) { out = '\b'; s->next_ms = now_ms + BLACK_TEXT_PAD_REPEAT_DELAY; }
    else if (x && s->x && s->next_ms && (int32_t)(now_ms - s->next_ms) >= 0) {
        out = '\b'; s->next_ms = now_ms + BLACK_TEXT_PAD_REPEAT_EVERY;
    } else if (y && !s->y) out = ' ';
    if (!x) s->next_ms = 0;
    s->x = x; s->y = y; s->start = start;
    return out;
}
#endif
