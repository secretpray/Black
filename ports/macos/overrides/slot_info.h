/* What the Saves page shows of a profile slot beyond the profile itself:
 * the mission last played and its difficulty, the time spent in missions
 * (counted by BLACK on this Mac, from the first save it saw), when the slot
 * was saved and a picture of the game from that run.
 *
 * Kept beside the saves, never in them: $XDG_DATA_HOME/xboxrecomp/SlotInfo/
 * slot-N.json and slot-N.png. The profile file, its format and its signature
 * are untouched. Each JSON names the slot's bytes as the game saved them
 * (their size and SHA-256), the picture's size and SHA-256 too: when the
 * profile no longer matches (an import, a restore, a save on another
 * machine), the page shows the profile's own data only; a picture that does
 * not match is left out. Nothing here ever blocks the game or an edit. */
#ifndef BLACK_SLOT_INFO_H
#define BLACK_SLOT_INFO_H
#include <stddef.h>
#include <stdint.h>
#include "black_profile.h"

enum { BLACK_SLOT_PICTURE_WIDTH = 480 };

/* One mission on one difficulty, as completed with this slot (mission_stats.h):
 * its bests and its last run. Accuracy in tenths of a percent, -1 without
 * shots. Zero completions: nothing recorded. */
typedef struct {
    int completions;
    double best_seconds; int fewest_deaths, best_accuracy;
    double last_seconds; int last_deaths, last_checkpoints, last_accuracy;
} BlackMissionBest;

typedef struct {
    BlackMissionBest missions[BLACK_PROFILE_MISSIONS]; /* difficulty * 8 + campaign position */
    int level, difficulty;      /* the game's level number and 0..3; -1 when unknown */
    uint64_t mission_seconds;   /* in missions, unpaused, with this slot */
    int64_t saved_at;           /* Unix time of the save */
    int has_picture;
    char picture[1024];         /* the PNG, when has_picture */
} BlackSlotInfo;

/* Game thread, each front-end tick. `profile` identifies the profile being
 * played (0: none); a change starts the count and the picture afresh.
 * `playing` is a mission in play: not paused, no settings panel. `level`
 * and `difficulty` are the mission's, or -1 outside one. */
void black_SlotInfoTick(double now, uint32_t profile, int playing, int level, int difficulty);

/* Game thread, after each successful profile save (0x1FE713): the saved
 * slot, its 0x778 bytes as written and the date the file carried before
 * this save (to carry the previous record over). A mission run this save
 * completes (mission_stats.h) joins the slot's bests. The files are written
 * on a queue of their own, off the game thread. */
void black_SlotInfoSaved(int slot, const uint8_t bytes[BLACK_PROFILE_SLOT], uint32_t previous_stamp);
/* Waits for the writes queued so far (tests, quitting). */
void black_SlotInfoFlush(void);

/* Presenter thread: 1 when a picture of the mission in play is wanted now
 * (every few seconds of play); then it hands the frame as BGRA, top row
 * first, with the display aspect it is shown at. Nearly black frames
 * (loading, fades) are not kept. */
int black_SlotInfoWantsFrame(double now);
void black_SlotInfoFrame(const uint8_t *bgra, uint32_t width, uint32_t height, size_t stride, double aspect, double now);

/* Panel: the record for `slot` of this profile image, 1 when there is one
 * that matches the slot's bytes. Never changes a file. */
int black_SlotInfoRead(const uint8_t profile[BLACK_PROFILE_SIZE], int slot, BlackSlotInfo *out);
/* The record's folder (tests and docs). */
int black_SlotInfoFolder(char *out, size_t cap);
#endif
