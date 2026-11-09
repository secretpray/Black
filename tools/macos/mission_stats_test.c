/* Mission runs (mission_stats.h): time while playing, deaths on the rising
 * edge, shots and hit shots, checkpoints; a reload keeps the run, another
 * mission or a quit from the pause menu starts afresh; a save ends the run
 * only with the mission recorded in the saved slot. */
#include "../../ports/macos/overrides/mission_stats.h"
#include <stdio.h>
#include <string.h>

static uint8_t slot[BLACK_PROFILE_SLOT];

/* The campaign record of (position, difficulty) with `result` (-1: none). */
static void record(int mission, int difficulty, uint32_t result)
{
    memcpy(slot + BLACK_PROFILE_RECORDS + (mission + difficulty * 8) * BLACK_PROFILE_RECORD, &result, 4);
}

static void play(double from, double to, int level, int difficulty, int playing, int paused)
{
    for (double t = from; t <= to + 1e-9; t += 0.1) black_StatsTick(t, 1, level, difficulty, playing, paused);
}

/* One update of the player's weapon: `hits` bullets land inside it. */
static void weapon(int fired, int hits)
{
    black_StatsWeaponBegin();
    while (hits-- > 0) black_StatsHit();
    black_StatsWeaponEnd(fired);
}

static int run(void)
{
    BlackMissionRun r;
    memset(slot, 0xFF, sizeof slot);
    if (black_mission_of_level(0) != 0 || black_mission_of_level(5) != 2 || black_mission_of_level(2) != -1 ||
        black_mission_of_level(8) != 7 || black_mission_of_level(9) != -1) return 1;

    /* Level 5 (mission 3) on Hard: 20 s played, 5 paused, one death. */
    play(0, 20, 5, 2, 1, 0);
    play(20.1, 25, 5, 2, 0, 1);
    play(25.1, 26, 5, 2, 1, 0);
    black_StatsDeath(); /* Mission Failed, Continue */
    play(26.1, 30, 5, 2, 1, 0);
    if (!black_StatsCurrent(&r) || r.mission != 2 || r.difficulty != 2 || r.deaths != 1 ||
        r.seconds < 24.5 || r.seconds > 25.5) return 2;
    /* Four shots: three pellets of the first land inside its update, the
     * third's lands after it; an update without a shot counts nothing. */
    weapon(1, 3);
    weapon(1, 0);
    weapon(0, 0);
    weapon(1, 0); black_StatsHit();
    weapon(1, 0);
    black_StatsHit(); black_StatsHit(); /* the fourth's: once */
    black_StatsCheckpoint(); black_StatsCheckpoint();
    if (!black_StatsCurrent(&r) || r.shots != 4 || r.hit_shots != 3 || r.checkpoints != 2) return 3;

    /* A reload passes out of the mission and back: the same run. */
    black_StatsTick(31, 0, -1, -1, 0, 0);
    play(35, 40, 5, 2, 1, 0);
    if (!black_StatsCurrent(&r) || r.deaths != 1 || r.shots != 4 || r.seconds < 29) return 4;

    /* A save without the mission's result does not end it; with one it does. */
    if (black_StatsTakeCompleted(slot, &r)) return 5;
    record(2, 2, 1);
    black_StatsTick(41, 0, -1, -1, 0, 0); /* the results, then the front end */
    black_StatsTick(50, 0, -1, -1, 0, 0);
    if (!black_StatsTakeCompleted(slot, &r) || r.mission != 2 || r.shots != 4) return 6;
    if (black_StatsCurrent(NULL) || black_StatsTakeCompleted(slot, &r)) return 7;

    /* Quit from the pause menu: given up; that save is not a completion. */
    play(100, 110, 5, 2, 1, 0);
    play(110.1, 111, 5, 2, 0, 1);
    black_StatsTick(111.5, 0, -1, -1, 0, 0);
    if (black_StatsTakeCompleted(slot, &r)) return 8;
    /* Entering it again starts afresh. */
    play(200, 205, 5, 2, 1, 0);
    if (!black_StatsCurrent(&r) || r.seconds > 5.5 || r.deaths) return 9;

    /* Another mission starts its own run; a save long after leaving is not it. */
    play(300, 310, 0, 1, 1, 0);
    if (!black_StatsCurrent(&r) || r.mission != 0 || r.difficulty != 1) return 10;
    black_StatsTick(311, 0, -1, -1, 0, 0);
    record(0, 1, 0);
    black_StatsTick(400, 0, -1, -1, 0, 0);
    if (black_StatsTakeCompleted(slot, &r)) return 11;

    /* The player's dying flag at a mission's end is no death: leaving then
     * and the save after it complete the run. */
    black_StatsReset();
    play(500, 505, 1, 0, 1, 0);
    black_StatsTick(507, 0, -1, -1, 0, 0);
    record(1, 0, 2);
    if (!black_StatsTakeCompleted(slot, &r) || r.deaths) return 12;
    /* Shots outside a run count for nothing. */
    black_StatsReset();
    weapon(1, 1); black_StatsHit(); black_StatsCheckpoint(); black_StatsDeath();
    if (black_StatsCurrent(NULL)) return 13;
    return 0;
}

int main(void)
{
    int result = run();
    if (result) fprintf(stderr, "mission stats test failed: %d\n", result);
    else printf("mission stats: runs, deaths, accuracy and completion PASS\n");
    return result;
}
