/* Generated checkpoint capture renamed by BlackGameOverlay.cmake, wrapped so
 * the title's own snapshot is written once, on the game thread. No per-frame
 * code: this runs only when the title itself captures a checkpoint. */
#include <stdio.h>
#include <stdlib.h>
#include "game_checkpoint.h"

void sub_001C8D20(void)
{
    if (getenv("RECOMP_BLACK_CHECKPOINT_TRACE"))
        fprintf(stderr, "[CHECKPOINT_TRACE] the title reached a checkpoint\n");
    black_OriginalCheckpointCapture();
    black_CheckpointEvent(1u, 0u);
}
