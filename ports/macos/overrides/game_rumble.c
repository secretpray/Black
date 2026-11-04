/* The normal native-C host bypasses USB; its XPP output entry must forward
 * the game's feedback, just as its input entry polls the host controller. */
#include "recomp_types.h"
#include "input/xinput_xbox.h"
#include "flash_memory.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>
extern size_t g_xbox_total_ram;
extern void black_InputFeedbackEvent(uint32_t token);
extern void black_OriginalInputState(void);
extern void black_OriginalVibrationMenu(void);
extern void black_OriginalVibrationPause(void);
#include "game_rumble.inc"
