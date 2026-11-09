/* See save_profiles.h. */
#import <Foundation/Foundation.h>
#include "save_profiles.h"
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void launch_backup(NSString *path);
static char used_path[1024];            /* the profile file the game last read or wrote */
static uint8_t want_before[BLACK_PROFILE_SIZE], want_after[BLACK_PROFILE_SIZE];
static int want_reason;
static const char *const reason_names[BLACK_SAVE_REASONS] = {"delete", "import", "restore", "launch"};
static atomic_int state, slot_in_use = -1, menu_seen;
static _Atomic double menu_at;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

/* $XDG_DATA_HOME/xboxrecomp: UserData/<title> holds the saves; ProfileBackups is ours. */
static NSString *data_root(void)
{
    const char *data = getenv("XDG_DATA_HOME");
    NSString *base = data && *data ? @(data) : [NSHomeDirectory() stringByAppendingPathComponent:@".local/share"];
    return [base stringByAppendingPathComponent:@"xboxrecomp"];
}
static NSString *save_root(void) { return [data_root() stringByAppendingPathComponent:@"UserData/" BLACK_SAVE_TITLE]; }
static NSString *backup_root(void) { return [data_root() stringByAppendingPathComponent:@"ProfileBackups"]; }

void black_SaveNoteUsed(const char *path)
{
    static const char marker[] = "/UserData/" BLACK_SAVE_TITLE "/";
    const char *at = path ? strstr(path, marker) : NULL;
    if (!at) return;
    at += sizeof marker - 1;
    const char *slash = strchr(at, '/');
    if (!slash || slash - at != 12 || strchr(slash + 1, '/') || !slash[1] || !strcmp(slash + 1, "SaveMeta.xbx")) return;
    char folder[16]; memcpy(folder, at, 12); folder[12] = 0;
    if (!black_save_folder_name(folder) || strlen(path) >= sizeof used_path) return;
    pthread_mutex_lock(&lock);
    int first = !used_path[0];
    if (strcmp(used_path, path)) { snprintf(used_path, sizeof used_path, "%s", path); fprintf(stderr, "[SAVES] profile file in use: %s/%s\n", folder, slash + 1); }
    pthread_mutex_unlock(&lock);
    if (first) @autoreleasepool { launch_backup(@(path)); }
}

static int read_exact(NSString *path, uint8_t out[BLACK_PROFILE_SIZE])
{
    NSData *d = [NSData dataWithContentsOfFile:path options:0 error:NULL];
    if (d.length != BLACK_PROFILE_SIZE) return 0;
    memcpy(out, d.bytes, BLACK_PROFILE_SIZE);
    return 1;
}

/* A save folder's profile file: its one file of 0x1800 bytes besides the .xbx metadata. */
static NSString *profile_in(NSString *folder)
{
    NSString *found = nil;
    for (NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:folder error:NULL]) {
        if ([name.pathExtension.lowercaseString isEqualToString:@"xbx"] || [name hasPrefix:@"."]) continue;
        NSString *path = [folder stringByAppendingPathComponent:name];
        NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:path error:NULL];
        if (![a.fileType isEqualToString:NSFileTypeRegular] || a.fileSize != BLACK_PROFILE_SIZE) continue;
        if (found) return nil;
        found = path;
    }
    return found;
}

static NSString *current_path(void)
{
    pthread_mutex_lock(&lock); NSString *path = used_path[0] ? @(used_path) : nil; pthread_mutex_unlock(&lock);
    if (path) return path;
    /* Before the game has read one: the newest profile under the save root. */
    NSString *newest = nil; NSDate *when = nil;
    for (NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:save_root() error:NULL]) {
        if (!black_save_folder_name(name.UTF8String)) continue;
        NSString *file = profile_in([save_root() stringByAppendingPathComponent:name]);
        NSDate *date = file ? [NSFileManager.defaultManager attributesOfItemAtPath:file error:NULL].fileModificationDate : nil;
        if (date && (!when || [date compare:when] == NSOrderedDescending)) { newest = file; when = date; }
    }
    return newest;
}

int black_SaveRead(uint8_t out[BLACK_PROFILE_SIZE], char *path, size_t cap)
{
    @autoreleasepool {
        NSString *file = current_path();
        if (path && cap) snprintf(path, cap, "%s", file ? file.UTF8String : "");
        return file && read_exact(file, out) && !black_profile_check(out, BLACK_PROFILE_SIZE);
    }
}

/* A profile with a used slot, anywhere under `folder` (no links followed);
 * a file beside a SaveMeta.xbx first. */
static int find_profile(NSString *folder, uint8_t out[BLACK_PROFILE_SIZE])
{
    NSDirectoryEnumerator *walk = [NSFileManager.defaultManager enumeratorAtURL:[NSURL fileURLWithPath:folder]
        includingPropertiesForKeys:@[NSURLIsRegularFileKey, NSURLFileSizeKey] options:0 errorHandler:nil];
    NSMutableArray<NSURL *> *found = [NSMutableArray array];
    for (NSURL *item in walk) {
        if (walk.level > 8) { [walk skipDescendants]; continue; }
        NSNumber *regular = nil, *size = nil;
        [item getResourceValue:&regular forKey:NSURLIsRegularFileKey error:NULL];
        [item getResourceValue:&size forKey:NSURLFileSizeKey error:NULL];
        if (regular.boolValue && size.unsignedLongLongValue == BLACK_PROFILE_SIZE) [found addObject:item];
        if (found.count > 64) break;
    }
    [found sortUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) {
        BOOL ma = [NSFileManager.defaultManager fileExistsAtPath:[a.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"SaveMeta.xbx"].path];
        BOOL mb = [NSFileManager.defaultManager fileExistsAtPath:[b.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"SaveMeta.xbx"].path];
        return ma != mb ? (ma ? NSOrderedAscending : NSOrderedDescending) : [a.path compare:b.path];
    }];
    for (NSURL *item in found) {
        BlackProfileSlot slots[BLACK_PROFILE_SLOTS];
        if (!read_exact(item.path, out) || !black_profile_slots(out, BLACK_PROFILE_SIZE, slots)) continue;
        for (int i = 0; i < BLACK_PROFILE_SLOTS; ++i) if (slots[i].active) return 1;
    }
    return 0;
}

/* A .zip unpacked by the system's ditto into a fresh temporary folder. */
static int read_zip(NSString *zip, uint8_t out[BLACK_PROFILE_SIZE])
{
    NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:zip error:NULL];
    if (!a || a.fileSize > 64ull << 20) return 0;
    NSString *tmp = NSTemporaryDirectory();
    NSString *pattern = [tmp stringByAppendingPathComponent:@"black-import-XXXXXX"];
    char dir[1024]; snprintf(dir, sizeof dir, "%s", pattern.fileSystemRepresentation);
    if (!mkdtemp(dir)) return 0;
    NSTask *task = [[[NSTask alloc] init] autorelease];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/ditto"];
    task.arguments = @[@"-x", @"-k", zip, @(dir)];
    task.standardOutput = NSFileHandle.fileHandleWithNullDevice; task.standardError = NSFileHandle.fileHandleWithNullDevice;
    int ok = 0;
    if ([task launchAndReturnError:NULL]) { [task waitUntilExit]; ok = task.terminationStatus == 0 && find_profile(@(dir), out); }
    [NSFileManager.defaultManager removeItemAtPath:@(dir) error:NULL];
    return ok;
}

int black_SaveReadImport(const char *path, uint8_t out[BLACK_PROFILE_SIZE])
{
    @autoreleasepool {
        if (!path) return 0;
        NSString *file = @(path); BOOL dir = NO;
        if (![NSFileManager.defaultManager fileExistsAtPath:file isDirectory:&dir]) return 0;
        if (dir) return find_profile(file, out);
        if ([file.pathExtension.lowercaseString isEqualToString:@"zip"]) return read_zip(file, out);
        BlackProfileSlot slots[BLACK_PROFILE_SLOTS];
        if (!read_exact(file, out) || !black_profile_slots(out, BLACK_PROFILE_SIZE, slots)) return 0;
        for (int i = 0; i < BLACK_PROFILE_SLOTS; ++i) if (slots[i].active) return 1;
        return 0;
    }
}

static NSString *import_root(void) { return [[data_root() stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Import"]; }

int black_SaveImportFolder(char *out, size_t cap)
{
    @autoreleasepool {
        NSString *root = import_root();
        if (![NSFileManager.defaultManager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:NULL]) return 0;
        snprintf(out, cap, "%s", root.fileSystemRepresentation);
        return 1;
    }
}

int black_SaveImports(BlackSaveImport *out, int max)
{
    @autoreleasepool {
        int count = 0;
        NSArray *names = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:import_root() error:NULL] sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
        for (NSString *name in names) {
            if (count >= max) break;
            if ([name hasPrefix:@"."]) continue;
            NSString *path = [import_root() stringByAppendingPathComponent:name];
            uint8_t data[BLACK_PROFILE_SIZE];
            if (!black_SaveReadImport(path.fileSystemRepresentation, data) ||
                !black_profile_slots(data, sizeof data, out[count].slots)) continue;
            snprintf(out[count].path, sizeof out[count].path, "%s", path.fileSystemRepresentation);
            snprintf(out[count].name, sizeof out[count].name, "%s", name.UTF8String);
            ++count;
        }
        return count;
    }
}

int black_SaveReadBackup(const char *path, uint8_t out[BLACK_PROFILE_SIZE])
{ @autoreleasepool { return path && read_exact(@(path), out) && !black_profile_check(out, BLACK_PROFILE_SIZE); } }

static NSArray<NSString *> *backup_names(void)
{
    NSMutableArray *names = [NSMutableArray array];
    for (NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:backup_root() error:NULL])
        if ([name hasSuffix:@".profile"]) [names addObject:name];
    [names sortUsingSelector:@selector(compare:)];
    return names;
}

int black_SaveBackups(BlackSaveBackup *out, int max)
{
    @autoreleasepool {
        int count = 0;
        for (NSString *name in backup_names().reverseObjectEnumerator) {
            if (count >= max) break;
            NSString *path = [backup_root() stringByAppendingPathComponent:name];
            uint8_t data[BLACK_PROFILE_SIZE];
            if (!read_exact(path, data) || !black_profile_slots(data, sizeof data, out[count].slots)) continue;
            snprintf(out[count].path, sizeof out[count].path, "%s", path.UTF8String);
            /* name: YYYY-MM-DD_HH-MM-SS-mmm[-n]-reason.profile */
            NSString *stamp = [name stringByDeletingPathExtension];
            out[count].reason = -1;
            for (int r = 0; r < BLACK_SAVE_REASONS; ++r)
                if ([stamp hasSuffix:[@"-" stringByAppendingString:@(reason_names[r])]]) out[count].reason = r;
            if (stamp.length >= 19) stamp = [NSString stringWithFormat:@"%@.%@ %@", [stamp substringWithRange:NSMakeRange(8, 2)],
                [stamp substringWithRange:NSMakeRange(5, 2)],
                [[stamp substringWithRange:NSMakeRange(11, 5)] stringByReplacingOccurrencesOfString:@"-" withString:@":"]];
            snprintf(out[count].when, sizeof out[count].when, "%s", stamp.UTF8String);
            ++count;
        }
        return count;
    }
}

static int trash(NSString *path)
{
    NSURL *url = [NSURL fileURLWithPath:path];
    if (![url.URLByDeletingLastPathComponent.path isEqualToString:backup_root()] || ![path hasSuffix:@".profile"]) return 0;
    NSError *error = nil;
    if (![NSFileManager.defaultManager trashItemAtURL:url resultingItemURL:NULL error:&error]) {
        fprintf(stderr, "[SAVES] backup not moved to the Trash: %s\n", error.localizedDescription.UTF8String);
        return 0;
    }
    return 1;
}
int black_SaveBackupTrash(const char *path)
{
    @autoreleasepool {
        int n = path ? trash(@(path)) : 0;
        if (n) fprintf(stderr, "[SAVES] backup moved to the Trash: %s\n", path);
        return n;
    }
}
int black_SaveBackupsTrashAll(void)
{
    @autoreleasepool {
        int n = 0;
        for (NSString *name in backup_names()) n += trash([backup_root() stringByAppendingPathComponent:name]);
        fprintf(stderr, "[SAVES] %d backups moved to the Trash\n", n);
        return n;
    }
}

void black_SaveRequest(const uint8_t before[BLACK_PROFILE_SIZE], const uint8_t edited[BLACK_PROFILE_SIZE], int reason)
{
    pthread_mutex_lock(&lock);
    want_reason = reason >= 0 && reason < BLACK_SAVE_REASONS ? reason : BLACK_SAVE_BEFORE_IMPORT;
    memcpy(want_before, before, BLACK_PROFILE_SIZE); memcpy(want_after, edited, BLACK_PROFILE_SIZE);
    atomic_store(&state, BLACK_SAVE_PENDING);
    pthread_mutex_unlock(&lock);
}
int black_SaveState(void) { return atomic_load(&state); }
int black_SaveSlotInUse(void) { return atomic_load(&slot_in_use); }
int black_SaveEditAllowed(void) { return atomic_load(&menu_seen) && now_s() - atomic_load(&menu_at) < 0.5; }

/* Writes beside the file and renames over it, then reads it back. */
static int replace_file(NSString *path, const uint8_t data[BLACK_PROFILE_SIZE])
{
    NSString *tmp = [path stringByAppendingFormat:@".%d.tmp", getpid()];
    int fd = open(tmp.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return 0;
    int ok = write(fd, data, BLACK_PROFILE_SIZE) == BLACK_PROFILE_SIZE && fsync(fd) == 0;
    ok &= close(fd) == 0;
    if (!ok || rename(tmp.fileSystemRepresentation, path.fileSystemRepresentation) != 0) { unlink(tmp.fileSystemRepresentation); return 0; }
    uint8_t back[BLACK_PROFILE_SIZE];
    return read_exact(path, back) && !memcmp(back, data, BLACK_PROFILE_SIZE);
}

/* A copy of the profile file in ProfileBackups/, named by time and reason;
 * the oldest past BLACK_SAVE_KEEP are removed. */
static int backup(NSString *path, int reason)
{
    NSError *error = nil;
    if (![NSFileManager.defaultManager createDirectoryAtPath:backup_root() withIntermediateDirectories:YES attributes:nil error:&error]) return 0;
    /* Milliseconds in the name keep two backups of one second in order. */
    static int64_t last_ms;
    struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
    int64_t ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (ms <= last_ms) ms = last_ms + 1; /* strictly later than the previous one */
    last_ms = ms;
    time_t seconds = (time_t)(ms / 1000);
    struct tm local; localtime_r(&seconds, &local);
    char stamp[40], day[32]; strftime(day, sizeof day, "%Y-%m-%d_%H-%M-%S", &local);
    snprintf(stamp, sizeof stamp, "%s-%03d", day, (int)(ms % 1000));
    NSString *target = [backup_root() stringByAppendingPathComponent:[NSString stringWithFormat:@"%s-%s.profile", stamp, reason_names[reason]]];
    for (int n = 2; [NSFileManager.defaultManager fileExistsAtPath:target]; ++n)
        target = [backup_root() stringByAppendingPathComponent:[NSString stringWithFormat:@"%s-%d-%s.profile", stamp, n, reason_names[reason]]];
    if (![NSFileManager.defaultManager copyItemAtPath:path toPath:target error:&error]) return 0;
    NSArray *names = backup_names();
    for (NSUInteger i = 0; i + BLACK_SAVE_KEEP < names.count; ++i)
        [NSFileManager.defaultManager removeItemAtPath:[backup_root() stringByAppendingPathComponent:names[i]] error:NULL];
    return 1;
}

/* At launch, as the game first reads its profile: a backup when the file
 * differs from the newest one (idea from Burnout Revenge's save_backup). */
static void launch_backup(NSString *path)
{
    uint8_t now[BLACK_PROFILE_SIZE], last[BLACK_PROFILE_SIZE];
    if (!read_exact(path, now) || black_profile_check(now, sizeof now)) return;
    NSString *newest = backup_names().lastObject;
    if (newest && read_exact([backup_root() stringByAppendingPathComponent:newest], last) && !memcmp(now, last, sizeof now)) return;
    if (backup(path, BLACK_SAVE_AT_LAUNCH)) fprintf(stderr, "[SAVES] profile backed up at launch\n");
}

int black_SaveExport(const uint8_t data[BLACK_PROFILE_SIZE], int slot, char *out, size_t cap)
{
    @autoreleasepool {
        uint8_t file[BLACK_PROFILE_SIZE]; memcpy(file, data, sizeof file);
        BlackProfileSlot slots[BLACK_PROFILE_SLOTS];
        if (!black_profile_slots(file, sizeof file, slots) || slot < 0 || slot >= BLACK_PROFILE_SLOTS || !slots[slot].active) return 0;
        for (int i = 0; i < BLACK_PROFILE_SLOTS; ++i) if (i != slot && slots[i].active) black_profile_clear(file, i);
        NSString *folder = [[data_root() stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Export"];
        if (![NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:NULL]) return 0;
        NSMutableString *name = [NSMutableString string];
        for (const char *c = slots[slot].name; *c; ++c) [name appendFormat:@"%c", isalnum((unsigned char)*c) ? *c : '_'];
        time_t t = time(NULL); struct tm local; localtime_r(&t, &local);
        char stamp[32]; strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &local);
        NSString *path = [folder stringByAppendingPathComponent:[NSString stringWithFormat:@"BLACK %@ %s.profile", name.length ? name : @"profile", stamp]];
        if (![[NSData dataWithBytes:file length:sizeof file] writeToFile:path atomically:YES]) return 0;
        snprintf(out, cap, "%s", path.fileSystemRepresentation);
        fprintf(stderr, "[SAVES] slot %d exported: %s\n", slot + 1, path.fileSystemRepresentation);
        return 1;
    }
}

static int apply(uint8_t *image)
{
    @autoreleasepool {
        NSString *path = current_path();
        uint8_t disk[BLACK_PROFILE_SIZE];
        if (!path || !read_exact(path, disk)) return BLACK_SAVE_NO_PROFILE;
        pthread_mutex_lock(&lock);
        int same_disk = !memcmp(disk, want_before, BLACK_PROFILE_SIZE);
        int same_image = image && !memcmp(image, want_before, BLACK_PROFILE_SIZE);
        int differ = 0;
        if (image) for (int i = 0; i < BLACK_PROFILE_SIZE; ++i) differ += image[i] != want_before[i];
        pthread_mutex_unlock(&lock);
        if (!same_disk || !same_image) {
            fprintf(stderr, "[SAVES] edit refused: file %s, game image %s (%d bytes differ)\n",
                same_disk ? "unchanged" : "changed", image ? "differs" : "missing", differ);
            return BLACK_SAVE_GAME_CHANGED;
        }
        if (!backup(path, want_reason)) { fprintf(stderr, "[SAVES] edit refused: no backup\n"); return BLACK_SAVE_WRITE_FAILED; }
        pthread_mutex_lock(&lock);
        int ok = replace_file(path, want_after);
        if (ok) memcpy(image, want_after, BLACK_PROFILE_SIZE);
        pthread_mutex_unlock(&lock);
        fprintf(stderr, "[SAVES] profile %s: %s\n", ok ? "written and the game's image updated" : "write failed; file left as it was", path.UTF8String);
        return ok ? BLACK_SAVE_DONE : BLACK_SAVE_WRITE_FAILED;
    }
}

void black_SaveGameTick(uint8_t *image, int slot, int main_menu)
{
    atomic_store(&slot_in_use, slot >= 0 && slot < BLACK_PROFILE_SLOTS ? slot : -1);
    atomic_store(&menu_seen, main_menu && image);
    if (main_menu && image) atomic_store(&menu_at, now_s());
    if (atomic_load(&state) != BLACK_SAVE_PENDING) return;
    atomic_store(&state, main_menu && image ? apply(image) : BLACK_SAVE_NOT_MENU);
}
