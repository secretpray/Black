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
static inline int black_film_skip(BlackFilmSkip *s, uint32_t player, uint32_t movie,
    const char name[9], uint32_t state, int background, int intros, int all)
{
    return black_film_skip_policy(s, player, movie, name, state, background, intros, 0, all);
}
#endif
