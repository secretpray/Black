/* The Saves tab: BLACK's profile slots on this Mac, backups and import.
 * The Xbox build has no delete or import (the console's dashboard did that).
 * The game reads its one profile file (black_profile.h) into the save
 * manager at [0x2D19BC]+0x1268 and writes that image back on every save, so
 * an edit to the file alone would be undone by the next autosave. An edit is
 * therefore applied on the game's thread, only on the main menu pages: the
 * image in memory must still equal the file, the file is backed up, written
 * whole and read back, and the same bytes replace the image. */
#ifndef BLACK_SAVE_PROFILES_H
#define BLACK_SAVE_PROFILES_H
#include <stddef.h>
#include <stdint.h>
#include "black_profile.h"

#define BLACK_SAVE_TITLE "45410083"
enum { BLACK_SAVE_BACKUPS = 12, BLACK_SAVE_IMPORTS = 12, BLACK_SAVE_KEEP = 30 };
/* Why a backup was made; in its file name (…-delete.profile). */
enum { BLACK_SAVE_BEFORE_DELETE, BLACK_SAVE_BEFORE_IMPORT, BLACK_SAVE_BEFORE_RESTORE, BLACK_SAVE_AT_LAUNCH, BLACK_SAVE_REASONS };

/* Result of the last edit request. */
enum { BLACK_SAVE_IDLE, BLACK_SAVE_PENDING, BLACK_SAVE_DONE, BLACK_SAVE_NOT_MENU,
       BLACK_SAVE_GAME_CHANGED, BLACK_SAVE_WRITE_FAILED, BLACK_SAVE_NO_PROFILE };

typedef struct { char path[1024]; char when[32]; int reason; BlackProfileSlot slots[BLACK_PROFILE_SLOTS]; } BlackSaveBackup;

/* The profile file the game reads, read whole; 1 when it is intact. */
int black_SaveRead(uint8_t out[BLACK_PROFILE_SIZE], char *path, size_t cap);
/* Newest first; returns how many. At most BLACK_SAVE_KEEP are kept. */
int black_SaveBackups(BlackSaveBackup *out, int max);
/* A profile to import, from a file of 0x1800 bytes, a folder holding one at
 * any depth (a save folder, UDATA from a dashboard tool) or a .zip of either
 * (unpacked by ditto into a temporary folder, removed after). A console's
 * signature is accepted; 1 when it has a profile. Nothing at `path` changes. */
int black_SaveReadImport(const char *path, uint8_t out[BLACK_PROFILE_SIZE]);
/* The import folder beside the data folder (Import/), made when missing:
 * what is put there is listed for the pad, no Finder needed. */
int black_SaveImportFolder(char *out, size_t cap);
typedef struct { char path[1024]; char name[128]; BlackProfileSlot slots[BLACK_PROFILE_SLOTS]; } BlackSaveImport;
/* Profiles found in the import folder, by name; returns how many. */
int black_SaveImports(BlackSaveImport *out, int max);
int black_SaveReadBackup(const char *path, uint8_t out[BLACK_PROFILE_SIZE]);
/* Backups to the Trash: one (a path black_SaveBackups gave) or all of them;
 * return how many were moved. Nothing else is touched. */
int black_SaveBackupTrash(const char *path);
int black_SaveBackupsTrashAll(void);

/* Panel side: asks for `edited` to replace `before`; then poll the state. */
void black_SaveRequest(const uint8_t before[BLACK_PROFILE_SIZE], const uint8_t edited[BLACK_PROFILE_SIZE], int reason);
/* One slot as a profile file of its own (the others empty) in Export/
 * beside the import folder; its path in `out`. 1 when written. */
int black_SaveExport(const uint8_t data[BLACK_PROFILE_SIZE], int slot, char *out, size_t cap);
int black_SaveState(void);
/* The slot the game autosaves to (its operator in use), or -1. */
int black_SaveSlotInUse(void);
/* Whether the game is on a main menu page where edits apply. */
int black_SaveEditAllowed(void);

/* Game thread, each menu frame: the save manager's image (NULL when none),
 * its autosave slot and whether this is a main menu page. */
void black_SaveGameTick(uint8_t *image, int slot, int main_menu);
/* Kernel file layer: a save file the game opened for reading or writing. */
void black_SaveNoteUsed(const char *host_path);

/* A profile folder name: twelve hex digits. */
static inline int black_save_folder_name(const char *name)
{
    int n = 0;
    for (; name[n]; ++n) {
        char c = name[n];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return n == 12;
}
#endif
