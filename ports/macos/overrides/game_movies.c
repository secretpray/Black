/* BLACK's film-update point, using this runtime's native C ABI. The policy
 * follows Burnout Revenge's selected-player/third-poll completion approach;
 * BLACK's own stop byte triggers its ordinary cleanup path. */
#include "recomp_types.h"
#include "film_policy.h"
#include "port_menu.h"
#include <stdlib.h>
#include <stdio.h>
extern void black_OriginalFilmTick(void);

/* Developer qualification only (launcher --dev-unlock-levels N, which also
 * gives the run its own save directory): open missions 0..N on every
 * difficulty in the live progress block G (0x2D0D80, USA), the way the title's
 * unreferenced debug routine 0x1F5A40 opens all of them, and make N the last
 * opened one, where mission select puts its cursor. Rewritten each front-end
 * tick, so a profile load cannot close them again. */
static void dev_unlock_levels(void)
{
    static int last = -2;
    if (last == -2) {
        const char *e = getenv("BLACK_DEV_UNLOCK_LEVELS");
        last = e ? atoi(e) : -1;
        if (last > 7) last = 7;
    }
    if (last < 0) return;
    const uint32_t g = 0x2D0D80u;
    for (uint32_t d = 0; d < 4; ++d) {
        MEM8(g + d) = (uint8_t)last;
        for (uint32_t level = 0; level < 8; ++level)
            MEM8(g + 4 + (d * 8 + level) * 0x20 + 0x1A) = level <= (uint32_t)last;
    }
}

void sub_00209DC0(void)
{
    dev_unlock_levels();
    static int configured, all;
    static BlackFilmSkip selection;
    if (!configured) {
        all = getenv("BLACK_SKIP_MOVIES") != NULL;
        configured = 1;
    }
    int intros = black_SettingsSkipIntros();
    int missions = black_SettingsSkipMissionIntros();
    uint32_t player = g_eax, game = MEM32(0x2D45F0);
    if (game && player == game + 0x20220 + 0x4C) {
        char name[9] = {0};
        for (int i = 0; i < 8; ++i) name[i] = MEM8(player + 0x84 + i);
        uint32_t film = MEM32(game + 0x2042C);
        int background = film < 27 ? MEM8(0x29D9C0 + film * 12 + 8) : 1;
        if (getenv("BLACK_FILM_LOG")) {
            static uint32_t previous_movie, previous_state, previous_film;
            uint32_t movie = MEM32(player), state = MEM32(player + 0x1A8);
            if (movie != previous_movie || state != previous_state || film != previous_film) {
                fprintf(stderr, "[FILM-STATE] frame=%u film=%u name=%s state=%X movie=%08X background=%d startup_done=%d startup=%d missions=%d\n",
                    MEM32(0x2F4368), film, name, state, movie, background, selection.startup_done, intros, missions);
                previous_movie = movie; previous_state = state; previous_film = film;
            }
        }
        if (black_film_skip_policy(&selection, player, MEM32(player), name,
            MEM32(player + 0x1A8), background, intros, missions, all) && !MEM8(player + 0x1B9)) {
            MEM8(player + 0x1B9) = 1;
            fprintf(stderr, "[FILM] %s: ordinary completion requested after 3 playing polls\n", name);
        }
    }
    black_OriginalFilmTick();
}
