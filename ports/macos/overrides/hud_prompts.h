#ifndef BLACK_HUD_PROMPTS_H
#define BLACK_HUD_PROMPTS_H
#include "flash_menu.h"
enum { BLACK_HINT_UNITS = 200 };
typedef struct {
    uint16_t raw[BLACK_HINT_UNITS], rendered[BLACK_HINT_UNITS];
    int valid, confirm;
    unsigned capacity;
    uint64_t signature;
} BlackHudPromptField;
typedef struct { uint32_t id; BlackHudPromptField body, continuation; } BlackHudPromptText;
typedef struct {
    uint32_t owner, hud, scratch;
    BlackHudPromptText pending, active;
    BlackHudPromptField pickup;
    uint16_t detail[BLACK_HINT_UNITS], glyph;
    unsigned position;
    uint8_t requested, current;
    int target;
} BlackHudPrompts;
/* Known formatter call sites only: handled means the wrapper returns cdecl ret. */
int black_HudPromptFormat(BlackHudPrompts *, const BlackFlashApi *, uint32_t caller, uint32_t buffer, uint32_t hud);
void black_HudPromptTick(BlackHudPrompts *, const BlackFlashApi *, uint32_t hud);
void black_HudPickupReady(BlackHudPrompts *, const BlackFlashApi *, uint32_t hud);
int black_PromptSetConfirmContext(int confirm);
uint64_t black_PromptSignature(void);
void black_PromptButtonLabel(int target, int menu, char *out, size_t capacity);
#endif
