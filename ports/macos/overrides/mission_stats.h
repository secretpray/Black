/* A mission run's statistics, counted by BLACK on this Mac: the time played
 * (not paused, no panel), the player's deaths (each Mission Failed answered
 * with Continue or Restart: the player's dying flag is set at a mission's end
 * too), the checkpoints the game captured, the player's shots and the shots
 * that hit someone. A run lasts from entering a mission until the game saves
 * the profile with that mission done, through its Continues and Restarts;
 * leaving it from the pause menu abandons it. No guest memory here: the game
 * thread hands in what it reads.
 *
 * The finished run reaches the slot's record beside the saves (slot_info.h),
 * where each mission keeps its best time, fewest deaths, best accuracy, the
 * number of completions and the last run. Never in the profile. */
#ifndef BLACK_MISSION_STATS_H
#define BLACK_MISSION_STATS_H
#include <stdint.h>
#include "black_profile.h"

typedef struct {
    int mission, difficulty;    /* 0..7 in campaign order, 0..3 */
    double seconds;
    uint32_t deaths, checkpoints, shots, hit_shots;
} BlackMissionRun;

/* The campaign position (0..7) of the game's level number, or -1; the order
 * of its table at 0x288648 (0, 1, 5, 3, 4, 6, 7, 8; 2 is loading). */
int black_mission_of_level(int level);

/* Game thread, each front-end tick. `level`/`difficulty` are the mission's
 * (-1 when unknown), `playing` counts time, `paused` is the game's pause
 * menu. */
void black_StatsTick(double now, int in_mission, int level, int difficulty, int playing, int paused);
/* Game thread, around the player's weapon update: its bullets hit inside
 * it, before the shot shows, so a hit there belongs to the shot `fired`
 * ends with. A hit outside it goes to the last shot that has none. */
void black_StatsWeaponBegin(void);
void black_StatsWeaponEnd(int fired);
void black_StatsHit(void);
/* Game thread, from the checkpoint capture and Mission Failed's answers. */
void black_StatsCheckpoint(void);
void black_StatsDeath(void);
/* Game thread, at a profile save: 1 with the run when it ends here, its
 * mission recorded with a result in the saved slot's bytes. The run is
 * then over. Time is the last tick's (the ticks' own clock). */
int black_StatsTakeCompleted(const uint8_t slot_bytes[BLACK_PROFILE_SLOT], BlackMissionRun *out);
/* The run in progress (tests, logs): 1 when there is one. */
int black_StatsCurrent(BlackMissionRun *out);
/* Tests: forget everything. */
void black_StatsReset(void);
#endif
