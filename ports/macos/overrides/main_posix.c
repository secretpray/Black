/* Keep the pinned game's host; extend the main-thread pump with optional
 * diagnostics. Film completion runs in the guest-thread wrapper instead.
 * The pinned host stays in its submodule. The include retains upstream's license. */
#define xbox_PumpMainRunLoop black_PumpMainRunLoop
#define xbox_FramebufferWindowStart black_FramebufferWindowStart
#define xbox_FramebufferWindowStop black_FramebufferWindowStop
#define main black_OriginalMain
#include BLACK_HOST_SOURCE
#undef main
#undef xbox_PumpMainRunLoop
#undef xbox_FramebufferWindowStart
#undef xbox_FramebufferWindowStop
#include <mach/mach_vm.h>
#include "port_menu.h"

extern char *black_AppPrepareGame(void);
extern char *black_InputGameView(const char *);
extern void black_InputGameViewRemove(const char *);
int main(int argc,char **argv)
{
    char *installed = argc > 1 ? NULL : black_AppPrepareGame();
    if (argc <= 1 && !installed) return 0;
    const char *game = argc > 1 ? argv[1] : installed;
    char *view = black_InputGameView(game);
    char **args = calloc((size_t)(argc > 1 ? argc : 2) + 1,sizeof(char *));
    if (!args) { black_InputGameViewRemove(view); free(view); free(installed); return 1; }
    args[0] = argv[0]; args[1] = view ? view : (char *)game;
    for (int i = 2; i < argc; ++i) args[i] = argv[i];
    setenv("BLACK_GAME_DIR",args[1],1);
    int result = black_OriginalMain(argc > 1 ? argc : 2,args);
    black_InputGameViewRemove(view); free(view); free(args); free(installed);
    return result;
}

extern void xbox_PumpMainRunLoop(void);
extern void xbox_FramebufferWindowStart(void);
extern void xbox_FramebufferWindowStop(void);

void black_FramebufferWindowStart(void)
{
    if (!getenv("BLACK_HEADLESS")) xbox_FramebufferWindowStart();
    else fprintf(stderr, "[HOST] Headless validation: real GPU context, no game window\n");
}

void black_FramebufferWindowStop(void)
{
    if (!getenv("BLACK_HEADLESS")) xbox_FramebufferWindowStop();
}

static int guest_read(uint32_t va, void *destination, size_t size)
{
    mach_vm_size_t copied = 0;
    if (!va || !g_xbox_mem_offset || (uint64_t)va + size > 0x100000000ull)
        return 0;
    return mach_vm_read_overwrite(mach_task_self(),
        (mach_vm_address_t)((uintptr_t)g_xbox_mem_offset + va), size,
        (mach_vm_address_t)destination, &copied) == KERN_SUCCESS && copied == size;
}

static uint32_t guest_u32(uint32_t va)
{
    uint32_t value = 0;
    guest_read(va, &value, sizeof value);
    return value;
}

extern void black_HostInputPump(void);
void black_PumpMainRunLoop(void)
{
    xbox_PumpMainRunLoop();
    black_HostInputPump();
    black_MenuPollHost();
    black_MenuPump();
    static int configured, telemetry;
    if (!configured) {
        telemetry = getenv("BLACK_STATE_LOG") != NULL;
        configured = 1;
    }
    if (!telemetry)
        return;
    static uint64_t last_ms;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (ms - last_ms < 1000u)
        return;
    last_ms = ms;
    /* Addresses from TGP's src/game/layout.h and hooks.cpp for the verified
     * USA XBE. Read defensively: allocations and scene pointers change. */
    uint32_t game_object = guest_u32(0x2D45F0);
    /* Front end and film player are embedded objects, not pointer fields. */
    uint32_t film_player = game_object ? game_object + 0x20220 + 0x4C : 0;
    uint32_t film_state = guest_u32(film_player + 0x1A8);
    char film_name[9] = {0};
    guest_read(film_player + 0x84, film_name, 8);
    static uint64_t last_report;
    if (telemetry && ms - last_report >= 1000) {
        last_report = ms;
        uint32_t world = guest_u32(0x2D45D4);
        if (getenv("BLACK_FILM_LOG")) {
            uint32_t ui = guest_u32(0x2D199C);
            char page[64] = {0};
            if (ui) guest_read(ui + 0x3840, page, sizeof page - 1);
            fprintf(stderr, "[FRONTEND] page=%s ui_state=%u profile_state=%u\n", page,
                ui ? guest_u32(ui + 0x381C) : 0,
                guest_u32(guest_u32(0x2D19BC) + 0x34));
        }
        float eye[3] = {0}, forward[3] = {0}, dt = 0, game_time = 0;
        if (world) {
            guest_read(world + 0x30 + 0x100, eye, sizeof eye);
            guest_read(world + 0x30 + 0xF0, forward, sizeof forward);
            guest_read(world + 0x1C, &dt, sizeof dt);
            guest_read(world + 0x20, &game_time, sizeof game_time);
        }
        fprintf(stderr, "[STATE] frame=%u world=%08X eye=%.4f,%.4f,%.4f forward=%.6f,%.6f,%.6f film=%s/%X wall_ms=%llu dt=%.6f game_time=%.6f\n",
                guest_u32(0x2F4368), world, eye[0], eye[1], eye[2],
                forward[0], forward[1], forward[2], film_name, film_state,
                (unsigned long long)ms, dt, game_time);
        /* The USA HUD calls health getter 0x13EC0 with world+0x30
         * (call site 0x1496BD); that getter reads actor+0x2F0. Read only.
         * These observations help distinguish death/retry from a frozen
         * world clock; a state value alone is not a qualified transition. */
        float health = 0;
        int health_read = world && guest_read(world + 0x30 + 0x2F0, &health, sizeof health);
        fprintf(stderr, "[LIFECYCLE] frame=%u actor=%08X health_read=%d health=%.4f game_state=%u\n",
                guest_u32(0x2F4368), world ? world + 0x30 : 0, health_read, health,
                game_object ? guest_u32(game_object + 0x21070) : 0);
        uint32_t device = guest_u32(0x27BFF8);
        if (device)
            fprintf(stderr, "[D3D] device=%08X submitted=%u completed=%u fence_event=%u\n",
                device, guest_u32(device + 0x2C), guest_u32(guest_u32(device + 0x30)),
                guest_u32(device + 0x1DCC + 4));
    }
}
