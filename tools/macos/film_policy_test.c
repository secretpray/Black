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
    /* Frontend weapon previews are not a campaign transition. They may play
     * between the logo and C_N_US/02_N while the startup skip is still On. */
    state = (BlackFilmSkip){0};
    const char *previews[] = {"AK_N", "Mag_N", "SMG_N", "MP5_N"};
    for (unsigned i = 0; i < sizeof previews / sizeof previews[0]; ++i) {
        char name[9] = {0}; strcpy(name, previews[i]);
        for (int poll = 0; poll < 5; ++poll)
            if (black_film_skip_policy(&state, 1, 20+i, name, 0x1C, 0, 1, 0, 0)) return 17;
        if (state.startup_done) return 18;
    }
    const char *remaining_intros[] = {"C_N_US", "02_N", "F02_P"};
    for (unsigned i = 0; i < sizeof remaining_intros / sizeof remaining_intros[0]; ++i) {
        char name[9] = {0}; strcpy(name, remaining_intros[i]);
        for (int poll = 0; poll < 3; ++poll)
            if (black_film_skip_policy(&state, 1, 30+i, name, 0x1C, 0, 1, 0, 0) != (poll == 2)) return 19;
    }
    /* Actual briefing/debrief/ending still seals the startup phase. */
    const char *campaign[] = {"03_N", "06_N", "F11_P", "F18_N"};
    for (unsigned i = 0; i < sizeof campaign / sizeof campaign[0]; ++i) {
        char name[9] = {0}; strcpy(name, campaign[i]); state = (BlackFilmSkip){0};
        black_film_skip_policy(&state,1,40+i,name,0x1C,0,1,0,0);
        if (!state.startup_done) return 20;
        for (int poll=0;poll<5;++poll)
            if(black_film_skip_policy(&state,1,50,"C_N_US",0x1C,0,1,0,0)) return 21;
    }
    /* Skip any film by button: a press the game ignores for 10 polls stops
     * the same film; one it answers moves on to another film, not stopped. */
    {
        BlackFilmPress p = {0};
        char lo[9] = "LO_N_US", next[9] = "AK_N";
        for (int i = 0; i < 3; ++i) if (black_film_press_policy(&p, 1, 0, 1, lo)) return 22;
        if (black_film_press_policy(&p, 1, 1, 1, lo)) return 23;            /* press */
        for (int i = 1; i < BLACK_FILM_PRESS_WAIT; ++i)
            if (black_film_press_policy(&p, 1, i < 4, 1, lo)) return 24;     /* held, then released */
        if (!black_film_press_policy(&p, 1, 0, 1, lo)) return 25;           /* tenth poll: stop */
        for (int i = 0; i < 20; ++i) if (black_film_press_policy(&p, 1, 0, 1, lo)) return 26;
        /* Held from before the film: no new press, nothing happens. */
        p = (BlackFilmPress){0};
        black_film_press_policy(&p, 1, 1, 0, lo);
        for (int i = 0; i < 20; ++i) if (black_film_press_policy(&p, 1, 1, 1, lo)) return 27;
        /* The game answered: another film plays at the tenth poll. */
        p = (BlackFilmPress){0};
        black_film_press_policy(&p, 1, 1, 1, lo);
        for (int i = 1; i < BLACK_FILM_PRESS_WAIT; ++i) black_film_press_policy(&p, 1, 0, 1, next);
        if (black_film_press_policy(&p, 1, 0, 1, next)) return 28;
        /* Looping menu backgrounds, idle player and Off are left alone. */
        p = (BlackFilmPress){0};
        black_film_press_policy(&p, 1, 1, 0, lo);
        for (int i = 0; i < 20; ++i) if (black_film_press_policy(&p, 1, 0, 0, lo)) return 29;
        p = (BlackFilmPress){0};
        black_film_press_policy(&p, 1, 1, 1, lo);
        for (int i = 1; i < BLACK_FILM_PRESS_WAIT; ++i) black_film_press_policy(&p, 1, 0, 1, lo);
        if (black_film_press_policy(&p, 1, 0, 0, lo)) return 30;           /* ended by itself */
        p = (BlackFilmPress){0};
        black_film_press_policy(&p, 0, 1, 1, lo);
        for (int i = 0; i < 20; ++i) if (black_film_press_policy(&p, 0, 0, 1, lo)) return 31;
    }
    /* The skip hint appears 2.5 s into a film that plays once and goes with it. */
    {
        BlackFilmHint h = {0};
        char lo[9] = "LO_N_US", c[9] = "C_N_US";
        if (black_film_hint_policy(&h, 1, 1, 0, 7, lo, 10.0)) return 40;
        if (black_film_hint_policy(&h, 1, 1, 0, 7, lo, 12.4)) return 41;
        if (!black_film_hint_policy(&h, 1, 1, 0, 7, lo, 12.5)) return 42;
        if (black_film_hint_policy(&h, 1, 1, 1, 7, lo, 13.0)) return 43;   /* stop requested */
        if (black_film_hint_policy(&h, 1, 1, 0, 8, c, 13.0)) return 44;    /* next film restarts */
        if (!black_film_hint_policy(&h, 1, 1, 0, 8, c, 15.6)) return 45;
        if (black_film_hint_policy(&h, 1, 0, 0, 8, c, 16.0)) return 46;    /* ended, menu loop */
        if (black_film_hint_policy(&h, 1, 1, 0, 8, c, 16.1)) return 47;    /* same film again: new start */
        if (black_film_hint_policy(&h, 0, 1, 0, 8, c, 30.0)) return 48;    /* option Off */
    }
    puts("Selected film / third playing poll / background and Off paths / skip by button / hint: PASS");
    return 0;
}
