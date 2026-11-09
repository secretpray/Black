/* BLACK's native checkpoint on disk: the generated-code capture wrapper and
 * the record writer. See game_checkpoint.h and game_checkpoint.inc. */
#include <stddef.h>
#include <stdlib.h>
#include "game_checkpoint.h"
#include "port_menu.h"

/* Defined by the runtime (xbox_memory_layout.c); its strong definition wins
 * in the game. */
__attribute__((weak)) ptrdiff_t g_xbox_mem_offset;

#include "game_checkpoint.inc"

void black_CheckpointEvent(uint32_t event, uint32_t core)
{
    /* The Saves row, or the developer test gate before the row is reachable. */
    if (!black_SettingsSaveCheckpoints() && !getenv("RECOMP_BLACK_CHECKPOINT_TEST")) return;
    black_cp_event(event, core);
}
