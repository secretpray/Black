#ifndef BLACK_FILM_POLICY_H
#define BLACK_FILM_POLICY_H
#include <stdint.h>
#include <string.h>
typedef struct {
    uint32_t player, movie;
    unsigned polls;
    int startup_done;
    char name[9];
} BlackFilmSkip;

static inline int black_film_number(const char name[9])
{
    const char *p = name[0] == 'F' ? name + 1 : name;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] != '_') return -1;
    return (p[0] - '0') * 10 + p[1] - '0';
}
static inline int black_film_is_mission(const char name[9])
{
    int number = black_film_number(name);
    /* Eight mission briefings in the verified film table. F11 is the
     * alternate fifth briefing. Ending 18/F18 and weapon films are excluded. */
    return number >= 3 && number <= 17 && (number & 1);
}

static inline int black_film_skip_policy(BlackFilmSkip *s, uint32_t player, uint32_t movie,
    const char name[9], uint32_t state, int background, int intros, int missions, int all)
{
    if (s->player != player || s->movie != movie || strcmp(s->name, name) || state != 0x1C) {
        s->player = player;
        s->movie = movie;
        s->polls = 0;
        memcpy(s->name, name, 9);
    }
    int intro = !strncmp(name, "LO_", 3) || !strncmp(name, "02_", 3) ||
        !strncmp(name, "F02_", 4) || !strncmp(name, "C_", 2) || !strncmp(name, "Attr_", 5);
    /* Menu weapon films (Mag/SMG/MP5, as well as AK) are foreground videos
     * too. Only a verified campaign briefing/debrief/ending closes startup;
     * otherwise visiting a menu disables the later C_/02_ intro skip. */
    int number = black_film_number(name);
    if (number >= 3 && number <= 18 && !background && state == 0x1C)
        s->startup_done = 1;
    if (!player || !movie || !name[0] || background || !strncmp(name, "AK_", 3) ||
        state != 0x1C || !(all || (intros && !s->startup_done && intro) ||
            (missions && black_film_is_mission(name)))) return 0;
    if (s->polls < 3) ++s->polls;
    return s->polls == 3;
}
/* Skip any film with A, START or Enter (Spark198rus/black-pc's movies.cpp).
 * BLACK skips a film only once it has been earned: the boot logos never, the
 * intro and a mission's films after that mission. A press that the game does
 * not answer within BLACK_FILM_PRESS_WAIT polls, while the same film still
 * plays once (not a looping menu background), requests the ordinary stop. A
 * film the game already lets you skip moves on by itself and the next one is
 * not stopped. */
#define BLACK_FILM_PRESS_WAIT 10
typedef struct {
    int was_down;
    unsigned wait;
    char name[9];
} BlackFilmPress;

static inline int black_film_press_policy(BlackFilmPress *s, int enabled, int down,
    int playing_once, const char name[9])
{
    int press = down && !s->was_down;
    s->was_down = down;
    if (!enabled) { s->wait = 0; return 0; }
    if (press && !s->wait && playing_once) {
        s->wait = BLACK_FILM_PRESS_WAIT;
        memcpy(s->name, name, 9);
        s->name[8] = 0;
        return 0;
    }
    if (s->wait && --s->wait == 0)
        return playing_once && !strncmp(name, s->name, 8);
    return 0;
}

/* The "A: SKIP" hint: from BLACK_FILM_HINT_DELAY seconds into a film that
 * plays once until it ends or its stop is requested. */
#define BLACK_FILM_HINT_DELAY 2.5
typedef struct {
    uint32_t movie;
    char name[9];
    double start;
} BlackFilmHint;

static inline int black_film_hint_policy(BlackFilmHint *h, int enabled, int playing_once,
    int stopping, uint32_t movie, const char name[9], double now)
{
    if (!playing_once) {
        h->movie = 0;
        h->name[0] = 0;
        return 0;
    }
    if (h->movie != movie || strncmp(h->name, name, 8)) {
        h->movie = movie;
        memcpy(h->name, name, 9);
        h->name[8] = 0;
        h->start = now;
    }
    return enabled && !stopping && now - h->start >= BLACK_FILM_HINT_DELAY;
}

static inline int black_film_skip(BlackFilmSkip *s, uint32_t player, uint32_t movie,
    const char name[9], uint32_t state, int background, int intros, int all)
{
    return black_film_skip_policy(s, player, movie, name, state, background, intros, 0, all);
}
#endif
