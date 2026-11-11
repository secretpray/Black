/* Slot records beside the saves (slot_info.h): written after a save, carried
 * over the next one, matched to the slot's bytes, the picture checked by its
 * hash; a record that does not match is not shown. Runs in a temporary data
 * folder of its own and removes it. */
#import <Cocoa/Cocoa.h>
#include "test_temp.h"
#include "../../ports/macos/overrides/slot_info.h"
#include "../../ports/macos/overrides/black_profile.h"
#include "../../ports/macos/overrides/mission_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* A sealed profile: slot 0 active as "KELLAR J." with `stamp`, the others empty. */
static void make_profile(uint8_t *data, uint32_t stamp)
{
    memset(data, 0, BLACK_PROFILE_SIZE);
    for (int slot = 0; slot < BLACK_PROFILE_SLOTS; ++slot) {
        uint8_t *s = data + slot * BLACK_PROFILE_SLOT;
        put32(s, BLACK_PROFILE_MARKER); put32(s + 4, BLACK_PROFILE_SLOT);
        if (slot) s[BLACK_PROFILE_EMPTY] = 1;
    }
    memcpy(data + BLACK_PROFILE_NAME, "KELLAR J.", 9);
    put32(data + BLACK_PROFILE_STAMP, stamp);
    black_profile_seal(data);
}

static void frame(uint8_t value, uint8_t *bgra) { memset(bgra, value, 640 * 480 * 4); }

static void play(double from, double to, uint32_t profile, int level, int difficulty)
{
    for (double t = from; t <= to + 1e-9; t += 0.1) black_SlotInfoTick(t, profile, 1, level, difficulty);
}

static int run(void)
{
    uint8_t profile[BLACK_PROFILE_SIZE], *pixels = malloc(640 * 480 * 4);
    BlackSlotInfo info;
    if (!pixels) return 1;
    make_profile(profile, 0x1111);
    if (black_profile_check(profile, sizeof profile)) return 2;
    if (black_SlotInfoRead(profile, 0, &info) || info.level != -1) return 3; /* nothing yet */

    /* Ten seconds of mission 5 (level 3) on Normal, a picture past the first three. */
    play(0, 10, 7, 3, 1);
    if (black_SlotInfoWantsFrame(2.0)) return 4;
    if (!black_SlotInfoWantsFrame(10.0) || black_SlotInfoWantsFrame(11.0)) return 5;
    frame(128, pixels);
    black_SlotInfoFrame(pixels, 640, 480, 640 * 4, 4.0 / 3.0, 10.0);
    frame(0, pixels); /* a load: not kept over the picture */
    black_SlotInfoFrame(pixels, 640, 480, 640 * 4, 4.0 / 3.0, 18.0);
    black_SlotInfoSaved(0, profile, 0);
    black_SlotInfoFlush();
    if (!black_SlotInfoRead(profile, 0, &info)) return 6;
    if (info.level != 3 || info.difficulty != 1 || info.mission_seconds < 9 || info.mission_seconds > 11 ||
        !info.has_picture || !info.saved_at) return 7;
    NSImage *image = [[[NSImage alloc] initWithContentsOfFile:@(info.picture)] autorelease];
    if (!image || (int)image.size.width != BLACK_SLOT_PICTURE_WIDTH || (int)image.size.height != 360) return 8;
    if (black_SlotInfoRead(profile, 1, &info)) return 9; /* an empty slot has none */

    /* The next save changes the date: the time adds up, the picture stays. */
    play(20, 25, 7, -1, -1);
    uint8_t next[BLACK_PROFILE_SIZE];
    make_profile(next, 0x2222);
    black_SlotInfoSaved(0, next, 0x1111);
    black_SlotInfoFlush();
    if (black_SlotInfoRead(profile, 0, &info)) return 10; /* the old bytes no longer match */
    if (!black_SlotInfoRead(next, 0, &info) || info.level != 3 || info.mission_seconds < 14 || info.mission_seconds > 16 ||
        !info.has_picture) return 11;

    /* A picture changed on disk is left out; the record still shows. */
    char dir[1024];
    if (!black_SlotInfoFolder(dir, sizeof dir)) return 12;
    NSString *png = [@(dir) stringByAppendingPathComponent:@"slot-1.png"];
    if (![[NSData dataWithBytes:"not a png" length:9] writeToFile:png atomically:YES]) return 13;
    if (!black_SlotInfoRead(next, 0, &info) || info.has_picture) return 14;

    /* A save whose previous date is not the record's starts afresh. */
    uint8_t other[BLACK_PROFILE_SIZE];
    make_profile(other, 0x3333);
    play(30, 32, 7, 4, 2);
    black_SlotInfoSaved(0, other, 0x9999);
    black_SlotInfoFlush();
    if (!black_SlotInfoRead(other, 0, &info) || info.mission_seconds > 3 || info.level != 4 || info.difficulty != 2 ||
        info.has_picture) return 15;

    /* Another profile in play drops this one's count and picture. */
    play(40, 50, 7, 3, 1);
    if (!black_SlotInfoWantsFrame(50.0)) return 16;
    frame(200, pixels);
    black_SlotInfoFrame(pixels, 640, 480, 640 * 4, 16.0 / 9.0, 50.0);
    play(51, 52, 9, -1, -1);
    uint8_t fourth[BLACK_PROFILE_SIZE];
    make_profile(fourth, 0x4444);
    black_SlotInfoSaved(0, fourth, 0x3333);
    black_SlotInfoFlush();
    if (!black_SlotInfoRead(fourth, 0, &info) || info.has_picture || info.mission_seconds > 6 || info.level != 4) return 17;

    /* A completed run joins the slot's missions: Normal, mission 3 (level 5). */
    black_StatsReset();
    for (double t = 100; t <= 160; t += 0.1) black_StatsTick(t, 1, 5, 1, 1, 0);
    for (int i = 0; i < 10; ++i) { black_StatsWeaponBegin(); if (i < 4) black_StatsHit(); black_StatsWeaponEnd(1); }
    black_StatsTick(161, 0, -1, -1, 0, 0);
    uint8_t done[BLACK_PROFILE_SIZE];
    make_profile(done, 0x5555);
    uint32_t met = 2;
    memcpy(done + BLACK_PROFILE_RECORDS + (2 + 1 * 8) * BLACK_PROFILE_RECORD, &met, 4);
    black_profile_seal(done);
    black_SlotInfoSaved(0, done, 0x4444);
    black_SlotInfoFlush();
    if (!black_SlotInfoRead(done, 0, &info)) return 19;
    const BlackMissionBest *b = &info.missions[1 * 8 + 2];
    if (b->completions != 1 || b->best_seconds < 59 || b->best_seconds > 61 || b->fewest_deaths || b->best_accuracy != 400 ||
        b->last_accuracy != 400 || info.missions[2].completions) return 20;
    /* A faster run with a death and worse aim: best time and accuracy kept apart. */
    for (double t = 200; t <= 240; t += 0.1) black_StatsTick(t, 1, 5, 1, 1, 0);
    black_StatsDeath();
    for (int i = 0; i < 10; ++i) { black_StatsWeaponBegin(); if (i < 2) black_StatsHit(); black_StatsWeaponEnd(1); }
    uint8_t again[BLACK_PROFILE_SIZE];
    memcpy(again, done, sizeof again);
    put32(again + BLACK_PROFILE_STAMP, 0x6666);
    black_profile_seal(again);
    black_SlotInfoSaved(0, again, 0x5555);
    black_SlotInfoFlush();
    if (!black_SlotInfoRead(again, 0, &info)) return 21;
    b = &info.missions[1 * 8 + 2];
    if (b->completions != 2 || b->best_seconds > 41 || b->fewest_deaths != 0 || b->best_accuracy != 400 ||
        b->last_deaths != 1 || b->last_accuracy != 200 || b->last_seconds > 41) return 22;
    /* A save that completes nothing keeps them. */
    uint8_t later[BLACK_PROFILE_SIZE];
    memcpy(later, again, sizeof later);
    put32(later + BLACK_PROFILE_STAMP, 0x7777);
    black_profile_seal(later);
    black_SlotInfoSaved(0, later, 0x6666);
    black_SlotInfoFlush();
    if (!black_SlotInfoRead(later, 0, &info) || info.missions[1 * 8 + 2].completions != 2) return 23;

    /* Not playing (paused, a menu) counts nothing and asks for no picture. */
    for (double t = 60; t <= 70; t += 0.1) black_SlotInfoTick(t, 9, 0, -1, -1);
    if (black_SlotInfoWantsFrame(70.0)) return 18;
    free(pixels);
    return 0;
}

int main(void)
{
    @autoreleasepool {
        char root[] = "/tmp/black-slot-info-XXXXXX";
        if (!black_test_temp_dir(root)) { perror("mkdtemp"); return 1; }
        setenv("XDG_DATA_HOME", root, 1);
        int result = run();
        [NSFileManager.defaultManager removeItemAtPath:@(root) error:NULL];
        if (result) fprintf(stderr, "slot info test failed: %d\n", result);
        else printf("slot info: records, carry-over, matching and pictures PASS\n");
        return result;
    }
}
