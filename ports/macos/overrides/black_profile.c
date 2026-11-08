/* See black_profile.h. */
#include "black_profile.h"
#include <CommonCrypto/CommonHMAC.h>
#include <string.h>

static uint32_t u32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8)); }

static uint32_t checksum(const uint8_t *data, int slot)
{
    uint32_t sum = 0;
    for (size_t at = 0; at < BLACK_PROFILE_PAYLOAD; at += 4) sum += u32(data + slot * BLACK_PROFILE_SLOT + at);
    return sum;
}

/* XCalculateSignature with this runtime's zero keys: HMAC-SHA1 with the
 * signature key over the file before the signature, then with the hard disk key. */
static void signature(const uint8_t *data, uint8_t out[CC_SHA1_DIGEST_LENGTH])
{
    static const uint8_t key[16];
    uint8_t inner[CC_SHA1_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA1, key, sizeof key, data, BLACK_PROFILE_SIGNATURE, inner);
    CCHmac(kCCHmacAlgSHA1, key, sizeof key, inner, sizeof inner, out);
}

static int active(const uint8_t *data, int slot)
{
    const uint8_t *s = data + slot * BLACK_PROFILE_SLOT;
    return u32(s) == BLACK_PROFILE_MARKER && s[BLACK_PROFILE_EMPTY] == 0;
}

int black_profile_check_unsigned(const uint8_t *data, size_t size)
{
    if (!data || size != BLACK_PROFILE_SIZE) return BLACK_PROFILE_BAD_SIZE;
    for (int slot = 0; slot < BLACK_PROFILE_SLOTS; ++slot) {
        const uint8_t *s = data + slot * BLACK_PROFILE_SLOT;
        uint32_t marker = u32(s);
        if ((marker != BLACK_PROFILE_MARKER && marker != 0) || u32(s + 4) != BLACK_PROFILE_SLOT) return BLACK_PROFILE_BAD_SLOT;
        if (marker == BLACK_PROFILE_MARKER && u32(s + BLACK_PROFILE_PAYLOAD) != checksum(data, slot)) return BLACK_PROFILE_BAD_SUM;
    }
    return BLACK_PROFILE_OK;
}

int black_profile_check(const uint8_t *data, size_t size)
{
    int problem = black_profile_check_unsigned(data, size);
    if (problem) return problem;
    uint8_t sig[CC_SHA1_DIGEST_LENGTH];
    signature(data, sig);
    return memcmp(sig, data + BLACK_PROFILE_SIGNATURE, sizeof sig) ? BLACK_PROFILE_BAD_SIGNATURE : BLACK_PROFILE_OK;
}

void black_profile_seal(uint8_t *data)
{
    for (int slot = 0; slot < BLACK_PROFILE_SLOTS; ++slot)
        if (u32(data + slot * BLACK_PROFILE_SLOT) == BLACK_PROFILE_MARKER)
            put32(data + slot * BLACK_PROFILE_SLOT + BLACK_PROFILE_PAYLOAD, checksum(data, slot));
    signature(data, data + BLACK_PROFILE_SIGNATURE);
}

int black_profile_slots(const uint8_t *data, size_t size, BlackProfileSlot out[BLACK_PROFILE_SLOTS])
{
    memset(out, 0, sizeof *out * BLACK_PROFILE_SLOTS);
    if (black_profile_check_unsigned(data, size)) return 0;
    for (int slot = 0; slot < BLACK_PROFILE_SLOTS; ++slot) {
        const uint8_t *s = data + slot * BLACK_PROFILE_SLOT;
        out[slot].damaged = u32(s) == 0;
        if (!(out[slot].active = active(data, slot))) continue;
        for (int i = 0; i < BLACK_PROFILE_NAME_SIZE && s[BLACK_PROFILE_NAME + i]; ++i) {
            uint8_t c = s[BLACK_PROFILE_NAME + i];
            out[slot].name[i] = c >= 0x20 && c < 0x7F ? (char)c : '?';
        }
        for (int m = 0; m < BLACK_PROFILE_MISSIONS; ++m) {
            uint32_t result = u32(s + BLACK_PROFILE_RECORDS + m * BLACK_PROFILE_RECORD);
            if (result <= 2) ++out[slot].completed;
        }
    }
    return 1;
}

int black_profile_free_slot(const uint8_t *data)
{
    for (int slot = 0; slot < BLACK_PROFILE_SLOTS; ++slot) if (!active(data, slot)) return slot;
    return -1;
}

/* An empty slot as the game writes one (consoles' saves): marker 0x26,
 * the empty flag at +0x76D set and its sum kept. Marker 0 is what the game
 * writes over slots it finds damaged (0x1F2F20), and its list shows them as
 * damaged, so it is never used for an empty slot. A damaged slot can be
 * cleared too; it becomes an empty one. */
int black_profile_clear(uint8_t *data, int slot)
{
    if (slot < 0 || slot >= BLACK_PROFILE_SLOTS || black_profile_check_unsigned(data, BLACK_PROFILE_SIZE)) return 0;
    uint8_t *s = data + slot * BLACK_PROFILE_SLOT;
    if (!active(data, slot) && u32(s) != 0) return 0;
    put32(s, BLACK_PROFILE_MARKER); s[BLACK_PROFILE_EMPTY] = 1;
    black_profile_seal(data);
    return 1;
}

int black_profile_copy(uint8_t *data, int slot, const uint8_t *source, int source_slot)
{
    if (slot < 0 || slot >= BLACK_PROFILE_SLOTS || source_slot < 0 || source_slot >= BLACK_PROFILE_SLOTS ||
        black_profile_check_unsigned(data, BLACK_PROFILE_SIZE) ||
        black_profile_check_unsigned(source, BLACK_PROFILE_SIZE) || !active(source, source_slot)) return 0;
    memcpy(data + slot * BLACK_PROFILE_SLOT, source + source_slot * BLACK_PROFILE_SLOT, BLACK_PROFILE_SLOT);
    black_profile_seal(data);
    return 1;
}
