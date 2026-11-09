/* See mission_stats.h. */
#include "mission_stats.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* A save this long after leaving the mission still ends its run (the
 * autosave after its results); a run left longer is over. */
enum { AFTER_LEAVING = 60 };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static BlackMissionRun run;
static int active, left, abandoned, shot_open, in_weapon, weapon_hit;
static double left_at, paused_at = -1e9, last_tick;

int black_mission_of_level(int level)
{
    static const int position[9] = {0, 1, -1, 3, 4, 2, 5, 6, 7};
    return level >= 0 && level < 9 ? position[level] : -1;
}

void black_StatsReset(void)
{
    pthread_mutex_lock(&lock);
    memset(&run, 0, sizeof run);
    active = left = abandoned = shot_open = in_weapon = weapon_hit = 0;
    left_at = last_tick = 0; paused_at = -1e9;
    pthread_mutex_unlock(&lock);
}

static void start(int mission, int difficulty)
{
    memset(&run, 0, sizeof run);
    run.mission = mission; run.difficulty = difficulty;
    active = 1; left = abandoned = shot_open = 0;
    fprintf(stderr, "[STATS] run started: mission %d difficulty %d\n", mission + 1, difficulty);
}

void black_StatsTick(double now, int in_mission, int level, int difficulty, int playing, int paused)
{
    pthread_mutex_lock(&lock);
    double dt = last_tick > 0 && now > last_tick ? now - last_tick : 0;
    last_tick = now;
    if (paused) paused_at = now;
    int mission = black_mission_of_level(level);
    if (in_mission && mission >= 0 && difficulty >= 0 && difficulty <= 3) {
        /* Another mission, a run left too long or given up: a new run. A
         * reload of the same one (Continue, Restart) keeps it. */
        if (!active || run.mission != mission || run.difficulty != difficulty ||
            (left && (abandoned || now - left_at > AFTER_LEAVING))) start(mission, difficulty);
        else if (left) left = 0;
        if (playing && dt < 0.5) run.seconds += dt; /* a stall or a sleep is not play */
    } else if (!in_mission && active && !left) {
        left = 1; left_at = now;
        abandoned = now - paused_at < 2.0; /* the pause menu's Quit */
    }
    pthread_mutex_unlock(&lock);
}

void black_StatsWeaponBegin(void)
{
    pthread_mutex_lock(&lock);
    in_weapon = 1; weapon_hit = 0;
    pthread_mutex_unlock(&lock);
}

void black_StatsWeaponEnd(int fired)
{
    pthread_mutex_lock(&lock);
    in_weapon = 0;
    if (fired && active && !left) {
        ++run.shots;
        /* A shot counts as a hit once, however many of its pellets land. */
        if (weapon_hit) { ++run.hit_shots; shot_open = 0; } else shot_open = 1;
        if (run.shots <= 3 || run.shots % 10 == 0) fprintf(stderr, "[STATS] %u shots, %u hit\n", run.shots, run.hit_shots);
    }
    weapon_hit = 0;
    pthread_mutex_unlock(&lock);
}

void black_StatsHit(void)
{
    pthread_mutex_lock(&lock);
    if (in_weapon) weapon_hit = 1;
    else if (active && !left && shot_open) { ++run.hit_shots; shot_open = 0; }
    pthread_mutex_unlock(&lock);
}

void black_StatsDeath(void)
{
    pthread_mutex_lock(&lock);
    if (active && !left) fprintf(stderr, "[STATS] death %u\n", ++run.deaths);
    pthread_mutex_unlock(&lock);
}

void black_StatsCheckpoint(void)
{
    pthread_mutex_lock(&lock);
    if (active && !left) ++run.checkpoints;
    pthread_mutex_unlock(&lock);
}

int black_StatsTakeCompleted(const uint8_t slot_bytes[BLACK_PROFILE_SLOT], BlackMissionRun *out)
{
    int done = 0;
    pthread_mutex_lock(&lock);
    double now = last_tick;
    if (active && slot_bytes && !(left && (abandoned || now - left_at > AFTER_LEAVING))) {
        const uint8_t *record = slot_bytes + BLACK_PROFILE_RECORDS + (run.mission + run.difficulty * 8) * BLACK_PROFILE_RECORD;
        uint32_t result;
        memcpy(&result, record, 4);
        if (result <= 2) {
            if (out) *out = run;
            active = 0; done = 1;
        }
    }
    pthread_mutex_unlock(&lock);
    return done;
}

int black_StatsCurrent(BlackMissionRun *out)
{
    pthread_mutex_lock(&lock);
    int has = active;
    if (has && out) *out = run;
    pthread_mutex_unlock(&lock);
    return has;
}
