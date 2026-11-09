#undef NDEBUG
#import <Foundation/Foundation.h>
#include "../../ports/macos/overrides/save_profiles.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8)); }

/* A profile as the game writes one: active slots with a name and records,
 * empty ones with marker 0 and the empty flag. */
static void make(uint8_t *d, const char *names[3])
{
    memset(d, 0, BLACK_PROFILE_SIZE);
    for (int s = 0; s < 3; ++s) {
        uint8_t *slot = d + s * BLACK_PROFILE_SLOT;
        put32(slot + 4, BLACK_PROFILE_SLOT);
        for (int m = 0; m < BLACK_PROFILE_MISSIONS; ++m) put32(slot + BLACK_PROFILE_RECORDS + m * BLACK_PROFILE_RECORD, m < s * 3 ? 2 : 0xFFFFFFFF);
        put32(slot, BLACK_PROFILE_MARKER); memset(slot + BLACK_PROFILE_EMPTY - 5, 1, 3);
        if (names[s]) strcpy((char *)slot + BLACK_PROFILE_NAME, names[s]);
        else { strcpy((char *)slot + BLACK_PROFILE_NAME, "KELLAR J."); slot[BLACK_PROFILE_EMPTY] = 1; }
    }
    black_profile_seal(d);
}

int main(void)
{
    @autoreleasepool {
        uint8_t a[BLACK_PROFILE_SIZE], b[BLACK_PROFILE_SIZE], c[BLACK_PROFILE_SIZE];
        BlackProfileSlot slots[3];
        const char *three[3] = {"rap", "fd", "ra"}, *one[3] = {"ra", NULL, NULL};
        make(a, three);
        assert(!black_profile_check(a, sizeof a));
        assert(black_profile_slots(a, sizeof a, slots) && slots[1].active && !strcmp(slots[1].name, "fd") && slots[2].completed == 6);
        assert(black_profile_free_slot(a) == -1);
        /* A broken checksum or signature is caught; size too. */
        memcpy(b, a, sizeof b); b[BLACK_PROFILE_SLOT + 0x200] ^= 1;
        assert(black_profile_check(b, sizeof b) == BLACK_PROFILE_BAD_SUM);
        memcpy(b, a, sizeof b); b[BLACK_PROFILE_SIGNATURE] ^= 1;
        assert(black_profile_check(b, sizeof b) == BLACK_PROFILE_BAD_SIGNATURE && !black_profile_check_unsigned(b, sizeof b));
        assert(black_profile_check(a, 100) == BLACK_PROFILE_BAD_SIZE);
        /* Delete: the slot reads empty, the rest is untouched, the file stays valid. */
        memcpy(b, a, sizeof b);
        assert(black_profile_clear(b, 1) && !black_profile_check(b, sizeof b));
        assert(black_profile_slots(b, sizeof b, slots) && !slots[1].active && slots[0].active && slots[2].active);
        assert(!memcmp(b, a, BLACK_PROFILE_SLOT) && black_profile_free_slot(b) == 1 && !black_profile_clear(b, 1));
        /* An empty slot as consoles write one: marker kept, empty flag set, summed. */
        assert(b[BLACK_PROFILE_SLOT] == BLACK_PROFILE_MARKER && b[BLACK_PROFILE_SLOT + BLACK_PROFILE_EMPTY] == 1);
        /* A slot the game wrote off (marker 0, its old sum) reads damaged and clears to empty. */
        { uint8_t g[BLACK_PROFILE_SIZE]; memcpy(g, a, sizeof g); put32(g, 0); black_profile_seal(g);
          assert(!black_profile_check(g, sizeof g) && black_profile_slots(g, sizeof g, slots) && slots[0].damaged && !slots[0].active);
          assert(black_profile_free_slot(g) == 0 && black_profile_clear(g, 0) && !black_profile_check(g, sizeof g));
          assert(black_profile_slots(g, sizeof g, slots) && !slots[0].damaged && !slots[0].active && g[0] == BLACK_PROFILE_MARKER); }
        /* Import: a slot from another file (a console signature) into a free slot or over one. */
        make(c, one); c[BLACK_PROFILE_SIGNATURE] ^= 0xFF;
        assert(black_profile_copy(b, 1, c, 0) && !black_profile_check(b, sizeof b));
        assert(black_profile_slots(b, sizeof b, slots) && !strcmp(slots[1].name, "ra"));
        assert(!black_profile_copy(b, 0, c, 1) && !black_profile_copy(b, 3, c, 0));

        /* The edit itself: the game's image must match, then file and image both change. */
        char dir[] = "/tmp/black-saves-XXXXXX"; assert(mkdtemp(dir));
        setenv("XDG_DATA_HOME", dir, 1);
        NSString *folder = [@(dir) stringByAppendingPathComponent:@"xboxrecomp/UserData/" BLACK_SAVE_TITLE "/03A47B43D696"];
        assert([NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:NULL]);
        NSString *file = [folder stringByAppendingPathComponent:@"ПРОФИЛЬ"];
        [[NSData dataWithBytes:"\xFF\xFEN\0" length:4] writeToFile:[folder stringByAppendingPathComponent:@"SaveMeta.xbx"] atomically:YES];
        assert([[NSData dataWithBytes:a length:sizeof a] writeToFile:file atomically:YES]);
        uint8_t read_back[BLACK_PROFILE_SIZE], image[BLACK_PROFILE_SIZE]; char path[1024];
        assert(black_SaveRead(read_back, path, sizeof path) && !memcmp(read_back, a, sizeof a));
        black_SaveNoteUsed(file.fileSystemRepresentation);
        memcpy(image, a, sizeof image);
        memcpy(b, a, sizeof b); assert(black_profile_clear(b, 2));
        /* Not on a main menu page: refused, nothing written. */
        black_SaveRequest(a, b, BLACK_SAVE_BEFORE_DELETE); black_SaveGameTick(image, 0, 0);
        assert(black_SaveState() == BLACK_SAVE_NOT_MENU && !black_SaveEditAllowed());
        /* The game has unsaved changes in its image: refused. */
        image[10] ^= 1;
        black_SaveRequest(a, b, BLACK_SAVE_BEFORE_DELETE); black_SaveGameTick(image, 0, 1);
        assert(black_SaveState() == BLACK_SAVE_GAME_CHANGED);
        image[10] ^= 1;
        black_SaveRequest(a, b, BLACK_SAVE_BEFORE_DELETE); black_SaveGameTick(image, 0, 1);
        assert(black_SaveState() == BLACK_SAVE_DONE && black_SaveEditAllowed() && black_SaveSlotInUse() == 0);
        assert(!memcmp(image, b, sizeof b) && black_SaveRead(read_back, NULL, 0) && !memcmp(read_back, b, sizeof b));
        /* The file before the edit is a backup, which restores like any edit. */
        BlackSaveBackup backups[BLACK_SAVE_BACKUPS];
        /* The launch backup (the game's first read) and the one before the delete. */
        assert(black_SaveBackups(backups, BLACK_SAVE_BACKUPS) == 2 && backups[0].slots[2].active);
        assert(backups[0].reason == BLACK_SAVE_BEFORE_DELETE && backups[1].reason == BLACK_SAVE_AT_LAUNCH);
        assert(black_SaveReadBackup(backups[0].path, c) && !memcmp(c, a, sizeof a));
        black_SaveRequest(b, c, BLACK_SAVE_BEFORE_RESTORE); black_SaveGameTick(image, 0, 1);
        assert(black_SaveState() == BLACK_SAVE_DONE && !memcmp(image, a, sizeof a));
        assert(black_SaveBackups(backups, BLACK_SAVE_BACKUPS) == 3 && !backups[0].slots[2].active && backups[0].reason == BLACK_SAVE_BEFORE_RESTORE);
        /* Only backups can be trashed: anything else is refused and left. */
        assert(!black_SaveBackupTrash(file.fileSystemRepresentation) && [NSFileManager.defaultManager fileExistsAtPath:file]);
        assert(!black_SaveBackupTrash("/etc/hosts") && !black_SaveBackupTrash(NULL));
        /* Export: the slot alone, the others empty, importable. */
        char exported[1024]; assert(black_SaveExport(a, 1, exported, sizeof exported));
        assert(black_SaveReadImport(exported, c) && black_profile_slots(c, sizeof c, slots) && slots[1].active && !slots[0].active && !slots[2].active);
        /* Import reads a file or a save folder. */
        assert(black_SaveReadImport(folder.fileSystemRepresentation, c) && !memcmp(c, a, sizeof a));
        assert(black_SaveReadImport(file.fileSystemRepresentation, c));
        assert(!black_SaveReadImport("/nonexistent", c));
        /* A dashboard tool's tree (UDATA/<title>/<save>/Profile) and a zip of it. */
        NSString *tree = [@(dir) stringByAppendingPathComponent:@"export/UDATA/45410083/1AAF1A0557B7"];
        assert([NSFileManager.defaultManager createDirectoryAtPath:tree withIntermediateDirectories:YES attributes:nil error:NULL]);
        [[NSData dataWithBytes:a length:sizeof a] writeToFile:[tree stringByAppendingPathComponent:@"Profile"] atomically:YES];
        [[NSData dataWithBytes:"\xFF\xFEN\0" length:4] writeToFile:[tree stringByAppendingPathComponent:@"SaveMeta.xbx"] atomically:YES];
        NSString *export = [@(dir) stringByAppendingPathComponent:@"export"];
        assert(black_SaveReadImport(export.fileSystemRepresentation, c) && !memcmp(c, a, sizeof a));
        char folder_path[1024]; assert(black_SaveImportFolder(folder_path, sizeof folder_path));
        NSString *zip = [@(folder_path) stringByAppendingPathComponent:@"black.10942.zip"];
        NSTask *task = [[[NSTask alloc] init] autorelease];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/ditto"];
        task.arguments = @[@"-c", @"-k", export, zip];
        assert([task launchAndReturnError:NULL]); [task waitUntilExit]; assert(task.terminationStatus == 0);
        memset(c, 0, sizeof c);
        assert(black_SaveReadImport(zip.fileSystemRepresentation, c) && !memcmp(c, a, sizeof a));
        [[NSData dataWithBytes:"junk" length:4] writeToFile:[@(folder_path) stringByAppendingPathComponent:@"notes.txt"] atomically:YES];
        BlackSaveImport found[BLACK_SAVE_IMPORTS];
        assert(black_SaveImports(found, BLACK_SAVE_IMPORTS) == 1 && !strcmp(found[0].name, "black.10942.zip") && found[0].slots[1].active);
        [NSFileManager.defaultManager removeItemAtPath:@(dir) error:NULL];
        puts("Saved profiles: slots, delete, import, backups, edit only against the game's image: PASS");
    }
    return 0;
}
