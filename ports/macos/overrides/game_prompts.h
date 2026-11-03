#ifndef BLACK_GAME_PROMPTS_H
#define BLACK_GAME_PROMPTS_H
#include "flash_menu.h"
typedef struct { uint32_t id, address; } BlackPromptText;
typedef struct {
    BlackFlashApi api;
    BlackPromptText texts[128];
    unsigned count, visits;
    uint64_t signature;
    int initialized;
} BlackGamePrompts;
uint32_t black_PromptString(BlackGamePrompts *, uint32_t id, uint32_t original);
void black_PromptTick(BlackGamePrompts *);
#endif
