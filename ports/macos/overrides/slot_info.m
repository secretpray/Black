/* See slot_info.h. */
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include "slot_info.h"
#include "display_gamma.h"
#include "mission_stats.h"
#include <CommonCrypto/CommonDigest.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* A picture every few seconds of play, once play has gone on for a while
 * (not the first frames after a load); the save keeps the newest. */
enum { FIRST_AFTER = 3, EVERY = 8, DARK = 6 };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t profile_key;
static double unsaved, last_tick, playing_since, last_request;
static int last_level = -1, last_difficulty = -1;
static uint8_t *picture;            /* BGRA, picture_w x picture_h */
static uint32_t picture_w, picture_h, picture_profile;
static atomic_int playing;

static NSData *png_data(const uint8_t *bgra, uint32_t w, uint32_t h);
static NSString *folder(void)
{
    const char *data = getenv("XDG_DATA_HOME");
    NSString *base = data && *data ? @(data) : [NSHomeDirectory() stringByAppendingPathComponent:@".local/share"];
    return [base stringByAppendingPathComponent:@"xboxrecomp/SlotInfo"];
}

int black_SlotInfoFolder(char *out, size_t cap)
{
    @autoreleasepool { return snprintf(out, cap, "%s", folder().fileSystemRepresentation) < (int)cap; }
}

static dispatch_queue_t queue(void)
{
    static dispatch_once_t once;
    static dispatch_queue_t q;
    dispatch_once(&once, ^{ q = dispatch_queue_create("black.slot-info", DISPATCH_QUEUE_SERIAL); });
    return q;
}

void black_SlotInfoFlush(void) { dispatch_sync(queue(), ^{}); }

static void drop_picture(void) { free(picture); picture = NULL; picture_w = picture_h = 0; }

void black_SlotInfoTick(double now, uint32_t profile, int is_playing, int level, int difficulty)
{
    pthread_mutex_lock(&lock);
    if (profile != profile_key) {
        profile_key = profile; unsaved = 0; last_level = last_difficulty = -1; drop_picture();
        last_tick = 0;
    }
    is_playing = is_playing && profile;
    if (is_playing && !atomic_load(&playing)) playing_since = now;
    if (is_playing && last_tick > 0) {
        double dt = now - last_tick;
        if (dt > 0 && dt < 0.5) unsaved += dt; /* a stall or a sleep is not play */
    }
    last_tick = now;
    if (profile && level >= 0 && difficulty >= 0 && difficulty <= 3) { last_level = level; last_difficulty = difficulty; }
    pthread_mutex_unlock(&lock);
    atomic_store(&playing, is_playing);
}

int black_SlotInfoWantsFrame(double now)
{
    if (!atomic_load(&playing)) return 0;
    pthread_mutex_lock(&lock);
    int want = now - playing_since >= FIRST_AFTER && now - last_request >= EVERY;
    if (want) last_request = now;
    pthread_mutex_unlock(&lock);
    return want;
}

/* Box-filtered to the picture's size at the display aspect, through the
 * game's own gamma ramp (the brightness the player saw); the player's own
 * display brightness and gamma are left out, as a screenshot would. */
void black_SlotInfoFrame(const uint8_t *bgra, uint32_t width, uint32_t height, size_t stride, double aspect, double now)
{
    (void)now;
    if (!bgra || width < 16 || height < 16 || stride < (size_t)width * 4) return;
    if (!(aspect >= 1.0 && aspect <= 4.0)) aspect = 4.0 / 3.0;
    uint32_t w = BLACK_SLOT_PICTURE_WIDTH, h = (uint32_t)(w / aspect + 0.5);
    uint8_t *out = malloc((size_t)w * h * 4);
    if (!out) return;
    uint8_t ramp[BLACK_GAMMA_ENTRIES * 3];
    black_GammaSnapshot(ramp);
    uint64_t luma = 0;
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t y0 = (uint32_t)((uint64_t)y * height / h), y1 = (uint32_t)((uint64_t)(y + 1) * height / h);
        if (y1 <= y0) y1 = y0 + 1;
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t x0 = (uint32_t)((uint64_t)x * width / w), x1 = (uint32_t)((uint64_t)(x + 1) * width / w);
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t sum[3] = {0, 0, 0}, n = 0;
            for (uint32_t sy = y0; sy < y1; ++sy) {
                const uint8_t *p = bgra + sy * stride + (size_t)x0 * 4;
                for (uint32_t sx = x0; sx < x1; ++sx, p += 4) { sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; ++n; }
            }
            uint8_t b = ramp[(sum[0] / n) * 3 + 2], g = ramp[(sum[1] / n) * 3 + 1], r = ramp[(sum[2] / n) * 3];
            uint8_t *q = out + ((size_t)y * w + x) * 4;
            q[0] = b; q[1] = g; q[2] = r; q[3] = 255;
            luma += (299u * r + 587u * g + 114u * b) / 1000u;
        }
    }
    unsigned mean = (unsigned)(luma / ((uint64_t)w * h));
    if (mean < DARK) { free(out); fprintf(stderr, "[SLOTINFO] picture skipped: dark frame (%u)\n", mean); return; } /* a load or a fade */
    fprintf(stderr, "[SLOTINFO] picture kept: %ux%u from %ux%u, mean %u\n", w, h, width, height, mean);
    /* Developer check (black.py run --slot-pictures): each kept picture as a file. */
    const char *dump = getenv("BLACK_SLOT_PICTURES");
    if (dump && *dump) @autoreleasepool {
        static int n;
        NSData *png = png_data(out, w, h);
        [png writeToFile:[NSString stringWithFormat:@"%s/picture-%03d.png", dump, ++n] atomically:YES];
    }
    pthread_mutex_lock(&lock);
    drop_picture();
    picture = out; picture_w = w; picture_h = h; picture_profile = profile_key;
    pthread_mutex_unlock(&lock);
}

static NSString *sha256(const void *data, size_t size)
{
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data, (CC_LONG)size, digest);
    NSMutableString *hex = [NSMutableString stringWithCapacity:sizeof digest * 2];
    for (size_t i = 0; i < sizeof digest; ++i) [hex appendFormat:@"%02x", digest[i]];
    return hex;
}

/* The player's name as the slot carries it, printable ASCII. */
static NSString *record_name(const uint8_t *bytes)
{
    char name[BLACK_PROFILE_NAME_SIZE + 1] = {0};
    for (int i = 0; i < BLACK_PROFILE_NAME_SIZE && bytes[BLACK_PROFILE_NAME + i]; ++i) {
        uint8_t c = bytes[BLACK_PROFILE_NAME + i];
        name[i] = c >= 0x20 && c < 0x7F ? (char)c : '?';
    }
    return @(name);
}

static uint32_t slot_stamp(const uint8_t *bytes)
{ uint32_t s; memcpy(&s, bytes + BLACK_PROFILE_STAMP, 4); return s; }

static NSDictionary *read_record(NSString *path)
{
    NSData *data = [NSData dataWithContentsOfFile:path options:0 error:NULL];
    if (!data || data.length > 64 * 1024) return nil;
    id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    return [json isKindOfClass:NSDictionary.class] && [json[@"format"] isEqual:@1] ? json : nil;
}

/* The picture the record names, when that file is still the same one. */
static int picture_matches(NSDictionary *record, NSString *dir)
{
    NSString *name = record[@"picture"], *hash = record[@"picture_sha256"];
    NSNumber *size = record[@"picture_size"];
    if (![name isKindOfClass:NSString.class] || ![hash isKindOfClass:NSString.class] || ![size isKindOfClass:NSNumber.class] ||
        [name containsString:@"/"]) return 0;
    NSData *png = [NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:name] options:0 error:NULL];
    return png && png.length == size.unsignedLongLongValue && [sha256(png.bytes, png.length) isEqualToString:hash];
}

static int write_atomically(NSData *data, NSString *path)
{
    NSString *tmp = [path stringByAppendingFormat:@".%d.tmp", getpid()];
    if (![data writeToFile:tmp options:0 error:NULL]) return 0;
    if (rename(tmp.fileSystemRepresentation, path.fileSystemRepresentation) != 0) { unlink(tmp.fileSystemRepresentation); return 0; }
    return 1;
}

static NSData *png_data(const uint8_t *bgra, uint32_t w, uint32_t h)
{
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef bitmap = CGBitmapContextCreate((void *)bgra, w, h, 8, (size_t)w * 4, space,
        kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst);
    CGImageRef image = bitmap ? CGBitmapContextCreateImage(bitmap) : NULL;
    NSMutableData *out = [NSMutableData data];
    CGImageDestinationRef dest = image ? CGImageDestinationCreateWithData((CFMutableDataRef)out, CFSTR("public.png"), 1, NULL) : NULL;
    int ok = 0;
    if (dest) { CGImageDestinationAddImage(dest, image, NULL); ok = CGImageDestinationFinalize(dest); CFRelease(dest); }
    if (image) CGImageRelease(image);
    if (bitmap) CGContextRelease(bitmap);
    CGColorSpaceRelease(space);
    return ok ? out : nil;
}

static int accuracy(const BlackMissionRun *run)
{ return run->shots ? (int)((uint64_t)run->hit_shots * 1000 / run->shots) : -1; }

/* The slot's missions with this run joined: completions, bests, the last. */
static NSDictionary *join_run(NSDictionary *missions, const BlackMissionRun *run)
{
    NSMutableDictionary *all = [NSMutableDictionary dictionary];
    if ([missions isKindOfClass:NSDictionary.class])
        for (NSString *key in missions) if ([missions[key] isKindOfClass:NSDictionary.class]) all[key] = missions[key];
    if (!run) return all;
    NSString *key = [NSString stringWithFormat:@"%d", run->difficulty * 8 + run->mission];
    NSDictionary *was = all[key];
    NSMutableDictionary *now = [NSMutableDictionary dictionary];
    int completions = [was[@"completions"] isKindOfClass:NSNumber.class] ? [was[@"completions"] intValue] : 0;
    double seconds = run->seconds;
    int deaths = (int)run->deaths, acc = accuracy(run);
    now[@"completions"] = @(completions + 1);
    now[@"best_seconds"] = @(completions && [was[@"best_seconds"] isKindOfClass:NSNumber.class] ? MIN([was[@"best_seconds"] doubleValue], seconds) : seconds);
    now[@"fewest_deaths"] = @(completions && [was[@"fewest_deaths"] isKindOfClass:NSNumber.class] ? MIN([was[@"fewest_deaths"] intValue], deaths) : deaths);
    int best_acc = completions && [was[@"best_accuracy"] isKindOfClass:NSNumber.class] ? [was[@"best_accuracy"] intValue] : -1;
    now[@"best_accuracy"] = @(MAX(best_acc, acc));
    now[@"last"] = @{@"seconds": @(seconds), @"deaths": @(deaths), @"checkpoints": @(run->checkpoints),
                     @"shots": @(run->shots), @"hit_shots": @(run->hit_shots), @"accuracy": @(acc),
                     @"at": @((int64_t)time(NULL))};
    all[key] = now;
    return all;
}

static void write_record(int slot, const uint8_t *bytes, uint32_t previous_stamp, double seconds,
    int level, int difficulty, const uint8_t *bgra, uint32_t w, uint32_t h, const BlackMissionRun *run)
{
    @autoreleasepool {
        NSString *dir = folder();
        if (![NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL]) {
            fprintf(stderr, "[SLOTINFO] folder not made: %s\n", dir.fileSystemRepresentation);
            return;
        }
        NSString *base = [NSString stringWithFormat:@"slot-%d", slot + 1];
        NSString *json_path = [dir stringByAppendingPathComponent:[base stringByAppendingPathExtension:@"json"]];
        NSString *name = record_name(bytes);
        uint32_t stamp = slot_stamp(bytes);
        /* The same slot's previous record: its date was the file's before
         * this save (or is this one, for a save that kept it). */
        NSDictionary *previous = read_record(json_path);
        NSString *was = [NSString stringWithFormat:@"%08X", previous_stamp], *now = [NSString stringWithFormat:@"%08X", stamp];
        int carry = previous && [previous[@"slot"] isEqual:@(slot + 1)] && [previous[@"name"] isEqual:name] &&
            ((previous_stamp && [previous[@"stamp"] isEqual:was]) || [previous[@"stamp"] isEqual:now]);
        NSMutableDictionary *record = [NSMutableDictionary dictionary];
        record[@"format"] = @1;
        record[@"slot"] = @(slot + 1);
        record[@"name"] = name;
        record[@"stamp"] = now;
        record[@"slot_size"] = @(BLACK_PROFILE_PAYLOAD);
        record[@"slot_sha256"] = sha256(bytes, BLACK_PROFILE_PAYLOAD);
        uint64_t total = (uint64_t)(seconds + 0.5);
        if (carry && [previous[@"mission_seconds"] isKindOfClass:NSNumber.class]) total += [previous[@"mission_seconds"] unsignedLongLongValue];
        record[@"mission_seconds"] = @(total);
        NSDictionary *missions = join_run(carry ? previous[@"missions"] : nil, run);
        if (missions.count) record[@"missions"] = missions;
        if (level < 0 && carry && [previous[@"level"] isKindOfClass:NSNumber.class] && [previous[@"difficulty"] isKindOfClass:NSNumber.class]) {
            level = [previous[@"level"] intValue]; difficulty = [previous[@"difficulty"] intValue];
        }
        if (level >= 0) { record[@"level"] = @(level); record[@"difficulty"] = @(difficulty); }
        time_t t = time(NULL);
        struct tm utc; gmtime_r(&t, &utc);
        char iso[32]; strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &utc);
        record[@"saved_at"] = @(iso);
        record[@"saved_at_unix"] = @((int64_t)t);
        if (bgra) {
            NSData *png = png_data(bgra, w, h);
            NSString *png_name = [base stringByAppendingPathExtension:@"png"];
            if (png && write_atomically(png, [dir stringByAppendingPathComponent:png_name])) {
                record[@"picture"] = png_name;
                record[@"picture_size"] = @(png.length);
                record[@"picture_sha256"] = sha256(png.bytes, png.length);
            } else fprintf(stderr, "[SLOTINFO] picture not written\n");
        } else if (carry && picture_matches(previous, dir)) {
            for (NSString *key in @[@"picture", @"picture_size", @"picture_sha256"]) record[key] = previous[key];
        }
        NSData *json = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
        int ok = json && write_atomically(json, json_path);
        if (run) fprintf(stderr, "[STATS] slot %d: mission %d difficulty %d completed in %.0f s, %u deaths, %u checkpoints, %u/%u shots hit\n",
            slot + 1, run->mission + 1, run->difficulty, run->seconds, run->deaths, run->checkpoints, run->hit_shots, run->shots);
        fprintf(stderr, "[SLOTINFO] slot %d %s: %llu s in missions, level %d, picture %s%s\n", slot + 1,
            ok ? "recorded" : "NOT RECORDED", (unsigned long long)total, level, record[@"picture"] ? "yes" : "no",
            carry ? "" : " (new record)");
    }
}

void black_SlotInfoSaved(int slot, const uint8_t bytes[BLACK_PROFILE_SLOT], uint32_t previous_stamp)
{
    if (!bytes || slot < 0 || slot >= BLACK_PROFILE_SLOTS) return;
    uint8_t *copy = malloc(BLACK_PROFILE_SLOT);
    if (!copy) return;
    memcpy(copy, bytes, BLACK_PROFILE_SLOT);
    pthread_mutex_lock(&lock);
    double seconds = unsaved; unsaved = 0;
    int level = last_level, difficulty = last_difficulty;
    uint8_t *frame = NULL; uint32_t w = 0, h = 0;
    if (picture && picture_profile == profile_key) {
        /* The picture goes with this save; the next save waits for a newer one. */
        frame = picture; w = picture_w; h = picture_h; picture = NULL; picture_w = picture_h = 0;
    }
    pthread_mutex_unlock(&lock);
    BlackMissionRun *run = malloc(sizeof *run);
    if (run && !black_StatsTakeCompleted(copy, run)) { free(run); run = NULL; }
    dispatch_async(queue(), ^{
        write_record(slot, copy, previous_stamp, seconds, level, difficulty, frame, w, h, run);
        free(copy); free(frame); free(run);
    });
}

int black_SlotInfoRead(const uint8_t profile[BLACK_PROFILE_SIZE], int slot, BlackSlotInfo *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    out->level = out->difficulty = -1;
    if (!profile || slot < 0 || slot >= BLACK_PROFILE_SLOTS) return 0;
    BlackProfileSlot slots[BLACK_PROFILE_SLOTS];
    if (!black_profile_slots(profile, BLACK_PROFILE_SIZE, slots) || !slots[slot].active) return 0;
    @autoreleasepool {
        const uint8_t *bytes = profile + slot * BLACK_PROFILE_SLOT;
        NSString *dir = folder();
        NSDictionary *record = read_record([dir stringByAppendingPathComponent:[NSString stringWithFormat:@"slot-%d.json", slot + 1]]);
        if (!record || ![record[@"slot"] isEqual:@(slot + 1)] || ![record[@"slot_size"] isEqual:@(BLACK_PROFILE_PAYLOAD)] ||
            ![record[@"slot_sha256"] isEqual:sha256(bytes, BLACK_PROFILE_PAYLOAD)]) return 0;
        if ([record[@"level"] isKindOfClass:NSNumber.class] && [record[@"difficulty"] isKindOfClass:NSNumber.class]) {
            int level = [record[@"level"] intValue], difficulty = [record[@"difficulty"] intValue];
            if (level >= 0 && level <= 8 && difficulty >= 0 && difficulty <= 3) { out->level = level; out->difficulty = difficulty; }
        }
        if ([record[@"mission_seconds"] isKindOfClass:NSNumber.class]) out->mission_seconds = [record[@"mission_seconds"] unsignedLongLongValue];
        if ([record[@"saved_at_unix"] isKindOfClass:NSNumber.class]) out->saved_at = [record[@"saved_at_unix"] longLongValue];
        NSDictionary *missions = record[@"missions"];
        if ([missions isKindOfClass:NSDictionary.class])
            for (int m = 0; m < BLACK_PROFILE_MISSIONS; ++m) {
                NSDictionary *one = missions[[NSString stringWithFormat:@"%d", m]], *last = one[@"last"];
                if (![one isKindOfClass:NSDictionary.class] || ![one[@"completions"] isKindOfClass:NSNumber.class]) continue;
                BlackMissionBest *b = &out->missions[m];
                b->completions = [one[@"completions"] intValue];
                b->best_seconds = [one[@"best_seconds"] doubleValue];
                b->fewest_deaths = [one[@"fewest_deaths"] intValue];
                b->best_accuracy = [one[@"best_accuracy"] isKindOfClass:NSNumber.class] ? [one[@"best_accuracy"] intValue] : -1;
                if ([last isKindOfClass:NSDictionary.class]) {
                    b->last_seconds = [last[@"seconds"] doubleValue];
                    b->last_deaths = [last[@"deaths"] intValue];
                    b->last_checkpoints = [last[@"checkpoints"] intValue];
                    b->last_accuracy = [last[@"accuracy"] isKindOfClass:NSNumber.class] ? [last[@"accuracy"] intValue] : -1;
                } else b->last_accuracy = -1;
            }
        if (picture_matches(record, dir)) {
            out->has_picture = 1;
            snprintf(out->picture, sizeof out->picture, "%s", [dir stringByAppendingPathComponent:record[@"picture"]].fileSystemRepresentation);
        }
        return 1;
    }
}
