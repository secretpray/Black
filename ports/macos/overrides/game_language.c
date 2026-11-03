/* The language word the build overlay puts in place of the USA start-up's
 * constant 1 (cmake/BlackGameOverlay.cmake, 0x1F8100). The game then loads
 * that language's string bank itself. */
#include "game_language.h"
#include <stdint.h>
#include <stdlib.h>

uint32_t black_GameLanguageIndex(void)
{
    int language = black_SettingsGameLanguage();
    const char *dir = getenv("BLACK_GAME_DIR");
    char built[1024];
    snprintf(built, sizeof built, "%s/language/strings/MainRu.bin", dir ? dir : "");
    if (language == BLACK_LANG_RUSSIAN && black_game_language_available(dir, language) && access(built, R_OK)) {
        fprintf(stderr, "[LANG] the Russian bank was not built; English\n");
        language = BLACK_LANG_ENGLISH;
    }
    if (!black_game_language_available(dir, language)) {
        if (language != BLACK_LANG_ENGLISH)
            fprintf(stderr, "[LANG] Main%s.bin is not in the installed game; English\n", black_game_language_suffix[language]);
        language = BLACK_LANG_ENGLISH;
    }
    if (language == BLACK_LANG_RUSSIAN) fprintf(stderr, "[LANG] game language Russian: MainUS.bin from MainRu.bin (index 1)\n");
    else fprintf(stderr, "[LANG] game language Main%s.bin (index %d)\n", black_game_language_suffix[language],
        black_game_language_index[language]);
    return (uint32_t)black_game_language_index[language];
}
