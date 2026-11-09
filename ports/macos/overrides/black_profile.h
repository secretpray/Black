/* BLACK's profile file: one save (folder named from FE_MCSAVE_PROFILE,
 * "Profile" / "ПРОФИЛЬ"), 0x1800 bytes, three operator slots of 0x778 and an
 * XCalculateSignature at 0x17EC. A slot starts with a 0x26 marker and its
 * size (marker 0: a slot the game found damaged and wrote off), carries its
 * save stamp at +0xC8, the name at +0xD0 and 32 campaign records at +0x124 (8 missions on 4 difficulties), and a sum of its 0x770 payload bytes
 * as 32-bit words at +0x770; an empty slot keeps the marker with +0x76D set,
 * as saves from consoles show.
 * Layout, checksums and signature as portingpete's launcher reads them
 * (launcher/src/core/Profile.cpp, MIT, 2041e5a9); the zero-key signature as
 * BlackDefinitiveHDPC's saveimport.cpp re-signs an imported profile. */
#ifndef BLACK_PROFILE_H
#define BLACK_PROFILE_H
#include <stddef.h>
#include <stdint.h>

enum {
    BLACK_PROFILE_SIZE = 0x1800, BLACK_PROFILE_SLOTS = 3, BLACK_PROFILE_SLOT = 0x778,
    BLACK_PROFILE_PAYLOAD = 0x770, BLACK_PROFILE_SIGNATURE = 0x17EC, BLACK_PROFILE_MARKER = 0x26,
    BLACK_PROFILE_NAME = 0xD0, BLACK_PROFILE_NAME_SIZE = 0x50, BLACK_PROFILE_EMPTY = 0x76D,
    BLACK_PROFILE_STAMP = 0xC8,
    BLACK_PROFILE_RECORDS = 0x124, BLACK_PROFILE_RECORD = 0x20, BLACK_PROFILE_MISSIONS = 32,
    BLACK_PROFILE_AVAILABLE = 0x1A
};

/* Why a file is not an intact profile; BLACK_PROFILE_OK when it is. */
enum { BLACK_PROFILE_OK, BLACK_PROFILE_BAD_SIZE, BLACK_PROFILE_BAD_SLOT, BLACK_PROFILE_BAD_SUM, BLACK_PROFILE_BAD_SIGNATURE };

typedef struct {
    int active, damaged;
    char name[BLACK_PROFILE_NAME_SIZE + 1];
    int completed; /* missions with a result, of 32 */
} BlackProfileSlot;

int black_profile_check(const uint8_t *data, size_t size);
/* As black_profile_check, but a console's own signature is not required:
 * for files from an Xbox or another console, signed again on import. */
int black_profile_check_unsigned(const uint8_t *data, size_t size);
void black_profile_seal(uint8_t *data);
/* Fills all three slots; returns 1 for an intact profile. */
int black_profile_slots(const uint8_t *data, size_t size, BlackProfileSlot out[BLACK_PROFILE_SLOTS]);
/* Edits in place and seals; return 1 when done, 0 when refused. */
int black_profile_clear(uint8_t *data, int slot);
int black_profile_copy(uint8_t *data, int slot, const uint8_t *source, int source_slot);
/* The first empty slot, or -1. */
int black_profile_free_slot(const uint8_t *data);
#endif
