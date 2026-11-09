/* BLACK's own native checkpoint, kept on disk between launches. The title's
 * in-memory checkpoint is written when it captures one and restored when the
 * mission loads, before its first streaming unit starts. Each save slot owns
 * the files whose record carries its player name.
 * Ported from portingpete/Black-2006-Recomp src/black_checkpoint.inc (MIT). */
#ifndef BLACK_GAME_CHECKPOINT_H
#define BLACK_GAME_CHECKPOINT_H
#include <stdint.h>

#define BLACK_CP_PATH_MAX 1024

/* One save slot's newest checkpoint, for the Saves page. */
typedef struct {
    uint32_t difficulty, level, stage, unit;
} BlackCheckpointInfo;

/* Events from the generated code: 1 after the title captured a checkpoint,
 * 2 before its first streaming unit starts, 4 after a successful profile
 * autosave. `core` is the event's guest pointer. Writes and reads nothing
 * unless the Saves page says so (or the developer test gate is set). */
void black_CheckpointEvent(uint32_t event, uint32_t core);

/* The profile image (black_profile.h) and its slot: the newest complete
 * checkpoint whose record names that slot's player; 1 when there is one. */
int black_CheckpointForSlot(const uint8_t *profile, int slot, BlackCheckpointInfo *out);
/* Removes every checkpoint file of that slot; returns how many. */
int black_CheckpointRemoveSlot(const uint8_t *profile, int slot);

/* Generated definition renamed by BlackGameOverlay.cmake. */
void black_OriginalCheckpointCapture(void);
#endif
