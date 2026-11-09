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
 * 2 before its first streaming unit starts (restores only an armed load),
 * 3 when the title's own RestartMission/RestartLevel starts the mission over
 * (removes its file unless F9 / Back + Y armed it), 4 after a successful
 * profile autosave. `core` is the event's guest pointer. Writes and reads nothing
 * unless the Saves page says so (or the developer test gate is set). */
void black_CheckpointEvent(uint32_t event, uint32_t core);

/* The game's own Autosave (its Profile options, main+0x20164), published by
 * the game thread; -1 until read. Checkpoints are written, restored and
 * offered only with both the Saves row and that Autosave on. */
void black_CheckpointSetAutosave(int on);
int black_CheckpointAutosave(void);
int black_CheckpointsActive(void);

/* The profile image (black_profile.h) and its slot: the newest complete
 * checkpoint whose record names that slot's player; 1 when there is one. */
int black_CheckpointForSlot(const uint8_t *profile, int slot, BlackCheckpointInfo *out);
/* Removes every checkpoint file of that slot; returns how many. */
int black_CheckpointRemoveSlot(const uint8_t *profile, int slot);

/* Restart from this run's checkpoint (F9 or Back + Y), confirmed first. The
 * request is set by any thread; the game's Flash tick takes it once and, in
 * a mission (`in_mission`) whose run has a checkpoint and a valid file,
 * answers 1: open the panel's confirmation. Its Yes confirms; the next take
 * checks again, arms the load and answers 2: run the pause menu's own
 * RestartLevel, whose reload the load hook restores. 0: nothing to do. */
void black_CheckpointRequestRestart(void);
void black_CheckpointConfirmRestart(void);
int black_CheckpointTakeRestart(int in_mission);

/* The title menu's checkpoint item: 1 when the active save slot has a
 * checkpoint this profile and an unlocked mission may resume. Prepare also
 * writes its level, stage, unit and difficulty where the title's own start
 * action reads them, and retires a live checkpoint so the file wins; the
 * load hook then restores the record. Game thread, only with the Saves
 * page's setting on. */
int black_CheckpointResumeOffer(void);
/* Prepare arms the next load for that mission. */
int black_CheckpointResumePrepare(void);

/* Generated definitions renamed by BlackGameOverlay.cmake. */
void black_OriginalCheckpointCapture(void);
void black_OriginalRestartMission(void);
void black_OriginalRestartLevel(void);
#endif
