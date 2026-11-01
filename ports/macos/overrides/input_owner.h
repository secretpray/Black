#ifndef BLACK_INPUT_OWNER_H
#define BLACK_INPUT_OWNER_H
#include <stdint.h>
enum { BLACK_INPUT_AUTO, BLACK_INPUT_GAMEPAD, BLACK_INPUT_KEYBOARD };
typedef struct { unsigned owner; uint32_t last_pad; int seen_pad, previous_key_action; } BlackInputOwner;
/* A deliberate keyboard key/button can change devices once the pad has
 * settled. Mouse motion alone never steals control from a gamepad. */
static inline unsigned black_input_owner(BlackInputOwner *s, unsigned policy,
    int connected, int pad_active, int keyboard_action, uint32_t now)
{
    int key_edge = keyboard_action && !s->previous_key_action;
    s->previous_key_action = keyboard_action;
    if (!connected) { s->owner = BLACK_INPUT_KEYBOARD; s->seen_pad = 0; return s->owner; }
    if (policy != BLACK_INPUT_AUTO) { s->owner = policy; return s->owner; }
    if (!s->seen_pad) { s->owner = BLACK_INPUT_GAMEPAD; s->seen_pad = 1; s->last_pad = now; }
    if (pad_active) { s->owner = BLACK_INPUT_GAMEPAD; s->last_pad = now; }
    else if (key_edge && (uint32_t)(now - s->last_pad) >= 750)
        s->owner = BLACK_INPUT_KEYBOARD;
    return s->owner;
}
#endif
