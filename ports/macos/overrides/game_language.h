/* Game text language. The USA executable fills Language/Strings/Main%s.bin
 * from its suffix table 0x288318 (0 UK, 1 US, 3 Ge, 4 Fr, 5 Sp, 6 It), indexed
 * by the language word 0x2D0D78, which its start-up 0x1F8100 fixes at 1 (US).
 * The European disc carries the other four banks; the USA disc has only
 * English. */
#ifndef BLACK_GAME_LANGUAGE_H
#define BLACK_GAME_LANGUAGE_H
#include <stdio.h>
#include <unistd.h>
#include "port_menu.h"

static const int black_game_language_index[BLACK_GAME_LANGUAGES] = {1, 4, 3, 5, 6};
static const char *const black_game_language_suffix[BLACK_GAME_LANGUAGES] = {"US", "Fr", "Ge", "Sp", "It"};

/* English always loads (MainUS.bin, the European MainUK.bin alias); another
 * language needs its bank in the installed game. */
static inline int black_game_language_available(const char *game_dir, int language)
{
    if (language == BLACK_LANG_ENGLISH) return 1;
    if (language < 0 || language >= BLACK_GAME_LANGUAGES || !game_dir || !*game_dir) return 0;
    char path[1024];
    snprintf(path, sizeof path, "%s/language/strings/Main%s.bin", game_dir, black_game_language_suffix[language]);
    return access(path, R_OK) == 0;
}
#endif
