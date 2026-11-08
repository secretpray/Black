/* D-pad repeat in the game's own menus. The title repeats a held direction
 * by its frame count, and its menus here run at about 60 frames a second,
 * so a held arrow skipped past the wanted item. A held direction reaches the
 * game as separate short presses instead: one at once, then one per period
 * after a pause. Each press lasts a few frames so a frame-based reader sees
 * it, and the gap after it is long enough to read as a release. */
#ifndef BLACK_MENU_REPEAT_H
#define BLACK_MENU_REPEAT_H
#include <stdint.h>

enum {
    BLACK_MENU_REPEAT_PRESS = 50,  /* ms a press is shown */
    BLACK_MENU_REPEAT_DELAY = 400, /* first repeat after this */
    BLACK_MENU_REPEAT_EVERY = 150  /* then one press per period */
};

/* mode per direction: 0 released, 1 stepping since start, 2 held from
 * outside a menu (ignored until released). */
typedef struct { uint8_t mode[4]; uint32_t start[4]; } BlackMenuRepeat;

/* held: the four direction bits now (bit i for direction i); active: a game
 * menu is up. Returns the bits the game should see. Outside menus the
 * buttons pass through unchanged. */
static inline uint32_t black_menu_repeat(BlackMenuRepeat *s, int active, uint32_t held, uint32_t now_ms)
{
    uint32_t out = 0;
    for (int i = 0; i < 4; ++i) {
        uint32_t bit = 1u << i;
        if (!(held & bit)) { s->mode[i] = 0; continue; }
        if (!active) { out |= bit; s->mode[i] = 2; continue; }
        if (s->mode[i] == 0) { s->mode[i] = 1; s->start[i] = now_ms; }
        if (s->mode[i] != 1) continue;
        uint32_t t = now_ms - s->start[i];
        if (t < BLACK_MENU_REPEAT_PRESS) out |= bit;
        else if (t >= BLACK_MENU_REPEAT_DELAY &&
                 (t - BLACK_MENU_REPEAT_DELAY) % BLACK_MENU_REPEAT_EVERY < BLACK_MENU_REPEAT_PRESS) out |= bit;
    }
    return out;
}
#endif
