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

/* The game's own Autosave (Profile options, main+0x20164), published by the
 * game thread: -1 until it is read. Checkpoints go to disk only beside saves
 * the game itself writes. */
static atomic_int black_cp_autosave = -1;

void black_CheckpointSetAutosave(int on) { atomic_store(&black_cp_autosave, !!on); }
int black_CheckpointAutosave(void) { return atomic_load(&black_cp_autosave); }
int black_CheckpointsActive(void)
{ return black_SettingsSaveCheckpoints() && atomic_load(&black_cp_autosave) != 0; }

void black_CheckpointEvent(uint32_t event, uint32_t core)
{
    /* The Saves row with the game's Autosave, or the developer test gate. */
    if (!black_CheckpointsActive() && !getenv("RECOMP_BLACK_CHECKPOINT_TEST")) return;
    black_cp_event(event, core);
}

/* F9 or Back + Y in the game only ask; the Flash tick checks the run and the
 * panel asks the player before the pause menu's own RestartLevel runs. The
 * panel's Y and Back keep their menu meanings. */
void black_CheckpointRequestRestart(void)
{
    if (!black_CheckpointsActive() || black_MenuIsOpen()) return;
    black_cp_restart_ask();
}

void black_CheckpointConfirmRestart(void)
{
    if (black_CheckpointsActive()) black_cp_restart_confirm();
}

int black_CheckpointTakeRestart(int in_mission)
{
    if (!black_CheckpointsActive()) {
        black_cp_restart_clear();
        return 0;
    }
    return black_cp_restart_take(in_mission);
}

int black_CheckpointResumeOffer(void)
{
    if (!black_CheckpointsActive()) return 0;
    return black_cp_resume_offer();
}

int black_CheckpointResumePrepare(void)
{
    if (!black_CheckpointsActive()) return 0;
    return black_cp_resume_prepare();
}
