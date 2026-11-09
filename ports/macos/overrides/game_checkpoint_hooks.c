/* Generated checkpoint capture and restarts renamed by BlackGameOverlay.cmake,
 * wrapped on the game thread. No per-frame code: these run only when the
 * title captures a checkpoint or restarts a mission. */
#include <stdio.h>
#include <stdlib.h>
#include "game_checkpoint.h"
#include "mission_stats.h"

void sub_001C8D20(void)
{
    if (getenv("RECOMP_BLACK_CHECKPOINT_TRACE"))
        fprintf(stderr, "[CHECKPOINT_TRACE] the title reached a checkpoint\n");
    black_OriginalCheckpointCapture();
    black_StatsCheckpoint(); /* mission statistics, whatever the Saves page says */
    black_CheckpointEvent(1u, 0u);
}

/* Mission Failed's Continue (InGameOn.bin: ContinueMission), back to the
 * checkpoint: a death for the mission statistics, as its Restart is. */
void black_OriginalContinueMission(void);
void sub_001240D0(void)
{
    black_StatsDeath();
    black_OriginalContinueMission();
}

/* Mission Failed's Restart (InGameOn.bin: RestartConfirm, then RestartMission). */
void sub_00124190(void)
{
    black_StatsDeath();
    black_CheckpointEvent(3u, 0u);
    black_OriginalRestartMission();
}

/* The pause menu's Restart (RestartConfirm, then RestartLevel); also what
 * F9 / Back + Y run once they armed the load. */
void sub_00124250(void)
{
    black_CheckpointEvent(3u, 0u);
    black_OriginalRestartLevel();
}
