#include "../../ports/macos/overrides/film_policy.h"
#include <stdio.h>
int main(void)
{
    BlackFilmSkip state = {0};
    const char logo[9] = "LO_N_US", menu[9] = "AK_N", story[9] = "03_N";
    if (black_film_skip(&state, 1, 2, logo, 4, 0, 1, 0)) return 1;
    for (int i = 0; i < 2; ++i)
        if (black_film_skip(&state, 1, 2, logo, 0x1C, 0, 1, 0)) return 2;
    if (!black_film_skip(&state, 1, 2, logo, 0x1C, 0, 1, 0)) return 3;
    for (int i = 0; i < 5; ++i) {
        if (black_film_skip(&state, 1, 3, menu, 0x1C, 0, 1, 1)) return 4;
        if (black_film_skip(&state, 1, 4, story, 0x1C, 0, 1, 0)) return 5;
        if (black_film_skip(&state, 1, 5, logo, 0x1C, 1, 1, 1)) return 6;
        if (black_film_skip(&state, 1, 6, logo, 0x1C, 0, 0, 0)) return 7;
    }
    state = (BlackFilmSkip){0};
    black_film_skip(&state, 1, 2, logo, 0x1C, 0, 1, 0);
    black_film_skip(&state, 1, 2, logo, 0x1C, 0, 1, 0);
    if (black_film_skip(&state, 1, 3, logo, 0x1C, 0, 1, 0)) return 8;
    black_film_skip(&state, 1, 4, story, 0x1C, 0, 1, 0);
    for (int i = 0; i < 5; ++i)
        if (black_film_skip(&state, 1, 5, logo, 0x1C, 0, 1, 0)) return 9;
    /* Ordinary play must never synthesize a stop, regardless of film or
     * whether the user has already entered a story scene. */
    state = (BlackFilmSkip){0};
    const char *films[] = {"LO_N_US", "C_N", "Attr_N", "02_N", "03_N", "AK_N"};
    for (unsigned film = 0; film < sizeof films / sizeof films[0]; ++film) {
        char name[9] = {0};
        strcpy(name, films[film]);
        for (int i = 0; i < 8; ++i)
            if (black_film_skip(&state, 1, film + 2, name, 0x1C, 0, 0, 0)) return 10;
    }
    /* The story bypass is separate from startup-only behavior. */
    state = (BlackFilmSkip){0};
    for (int i = 0; i < 2; ++i)
        if (black_film_skip(&state, 1, 2, story, 0x1C, 0, 0, 1)) return 11;
    if (!black_film_skip(&state, 1, 2, story, 0x1C, 0, 0, 1)) return 12;
    const char *mission_films[] = {"03_N", "05_P", "07_N", "09_P", "11_N", "F11_P", "13_N", "15_P", "17_N"};
    for (unsigned i = 0; i < sizeof mission_films / sizeof mission_films[0]; ++i) {
        char name[9] = {0}; strcpy(name, mission_films[i]);
        if (!black_film_is_mission(name)) return 13;
        state = (BlackFilmSkip){0};
        for (int poll = 0; poll < 3; ++poll)
            if (black_film_skip_policy(&state, 1, i + 2, name, 0x1C, 0, 0, 1, 0) != (poll == 2)) return 14;
    }
    const char *protected_films[] = {"18_N", "F18_P", "20_N", "AK_N", "C_N_US", "LO_N_US", "SMG_N", "02_N", "F02_P"};
    for (unsigned i = 0; i < sizeof protected_films / sizeof protected_films[0]; ++i) {
        char name[9] = {0}; strcpy(name, protected_films[i]); state = (BlackFilmSkip){0};
        if (black_film_is_mission(name)) return 15;
        for (int poll = 0; poll < 5; ++poll)
            if (black_film_skip_policy(&state, 1, i + 2, name, 0x1C, 0, 0, 1, 0)) return 16;
    }
    puts("Selected film / third playing poll / background and Off paths: PASS");
    return 0;
}
