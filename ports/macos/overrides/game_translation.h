/* Russian game text, built in the player's own installed copy from its English
 * string bank and the translation text shipped with the app (no game data is
 * shipped). The game keeps running as English (language word 1, MainUS.bin);
 * before each start MainUS.bin is made the Russian or the English bank.
 *
 * The retail Big.bin/Small.bin '4.1v' fonts have Latin-1 only. Cyrillic is
 * added as new glyph records with their real code points, drawn from a system
 * font into free atlas space. The game finds a glyph through 128 buckets
 * (code & 127) at 0x20, each the first of the records sorted by
 * (code & 127, code) that end at the default glyph (0x1C); records are
 * re-sorted and the buckets rebuilt, so every other language keeps its text. */
#pragma once
#import <Foundation/Foundation.h>

/* The shipped translation: russian-strings.json in the app, or
 * BLACK_RUSSIAN_STRINGS outside it; nil when there is none. */
NSURL *black_RussianStrings(void);
/* {id hex: text or NSNull (keep the English string)} from the shipped JSON. */
NSDictionary<NSString *, id> *black_TranslationLoad(NSURL *json, NSError **error);
/* A string bank with the same IDs and order as `english`, texts replaced. */
NSData *black_TranslatedBank(NSData *english, NSDictionary<NSString *, id> *strings, NSError **error);
/* The font with А-Я, Ё and their lower case added (the font is capitals only),
 * drawn from `systemFont`; returns the input when it already has them. */
NSData *black_FontWithCyrillic(NSData *font, NSString *systemFont, NSError **error);

/* Idempotent, in the installed game: keeps the English bank as MainEn.bin and
 * the retail fonts as *.retail.bin, adds Cyrillic to the fonts and writes
 * MainRu.bin from `strings` (a missing file leaves Russian unavailable). */
BOOL black_PrepareRussian(NSURL *game, NSURL *strings, NSError **error);
/* Makes MainUS.bin the Russian bank when `russian`, else the English one. */
BOOL black_ApplyGameText(NSURL *game, BOOL russian, NSError **error);
