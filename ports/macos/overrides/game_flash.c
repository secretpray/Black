/* Guest calls in the pinned runtime's native-C ABI. Restore every virtual
 * register, including x87/MMX/SSE/SEH, after each Flash method invocation. */
/* MMX declarations in the pinned header are inside this guard. */
#define RECOMP_GENERATED_CODE 1
#include "recomp_types.h"
#include "flash_menu.h"
#include "flash_memory.h"
#include "game_prompts.h"
#include "game_checkpoint.h"
#include "hud_prompts.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include "port_menu.h"
extern uint32_t xbox_HeapAlloc(uint32_t,uint32_t);
extern size_t g_xbox_total_ram;
static BlackFlashMenu menu;
static BlackGamePrompts prompts;
static _Thread_local BlackHudPrompts hud_prompts;
static int memory_ok(uint32_t at,size_t size)
{ return black_flash_memory_ok(at,size,g_xbox_total_ram); }
static int read_guest(void *opaque,uint32_t at,void *out,size_t size)
{ (void)opaque;if(!memory_ok(at,size))return 0;memcpy(out,(const void *)XBOX_PTR(at),size);return 1; }
static int write_guest(void *opaque,uint32_t at,const void *in,size_t size)
{ (void)opaque;if(!memory_ok(at,size))return 0;memcpy((void *)XBOX_PTR(at),in,size);return 1; }
static uint32_t alloc_guest(void *opaque,size_t size)
{ (void)opaque;return xbox_HeapAlloc((uint32_t)size,16); }
#include "flash_guest_call.inc"
static void trace_guest(void *opaque,const char *page,uint32_t list,unsigned count)
{ (void)opaque;if(getenv("RECOMP_INPUT_DEBUG"))fprintf(stderr,"[FLASH-MENU] page=%s list=%08X reachable=%u\n",page,list,count); }
static int checkpoint_guest(void *opaque,int mode)
{ (void)opaque;return mode==2?black_CheckpointsActive():mode?black_CheckpointResumePrepare():black_CheckpointResumeOffer(); }
static pthread_once_t api_once = PTHREAD_ONCE_INIT;
static void initialize_api(void)
{ menu.api=(BlackFlashApi){NULL,read_guest,write_guest,call_guest,alloc_guest,trace_guest,getenv("RECOMP_INPUT_DEBUG")!=NULL,checkpoint_guest}; prompts.api=menu.api; }
static void initialize(void) { pthread_once(&api_once,initialize_api); }
/* The Home quit dialog's "quit to main menu" (port_menu.m): it sends START
 * unless the pause menu is already up; here, once that page can take a
 * call, its own GoToQuitConfirm asks the player, in the game's dialog. */
#include <stdatomic.h>
#include <time.h>
#include "port_menu.h"
static double monotonic(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static _Atomic double pause_seen;
int black_GameInMission(void)
{
    uint32_t game = MEM32(0x2D45F0), state = game ? MEM32(game + 0x21070) : 0;
    return state && state != game + 0x20220;
}
int black_GamePaused(void) { return monotonic() - atomic_load(&pause_seen) < 0.3; }
static void quit_to_menu(uint32_t ui)
{
    static double asked;
    if (black_FlashOnPage(&menu, ui, "PauseMenu")) atomic_store(&pause_seen, monotonic());
    if (black_QuitToMenuTake()) asked = monotonic();
    if (!asked) return;
    int called = black_FlashPauseQuit(&menu, ui);
    if (called > 0) {
        fprintf(stderr, "[HOME] pause menu: the game's quit confirmation\n");
        asked = 0;
    } else if (monotonic() - asked > 5) {
        fprintf(stderr, "[HOME] quit to main menu: no pause menu within 5 s\n");
        asked = 0;
    }
}
/* F9 or Back + Y asked for a restart from the checkpoint: first the panel
 * asks the player; once confirmed, the next load is armed for this mission
 * and the pause menu's own in-mission Restart (RestartLevel) runs here, where
 * that menu runs it too. It clears the live checkpoint, so the level reload
 * reaches the load hook, which restores the armed file; unarmed, the same
 * action restarts from the start. */
static void restart_tick(void)
{
    int take = black_CheckpointTakeRestart(black_GameInMission());
    if (take == 1) { black_MenuCommand(BLACK_MENU_RESTART); return; }
    if (take != 2) return;
    if (!recomp_lookup_manual(0x00124250u) && !recomp_lookup(0x00124250u)) {
        fprintf(stderr, "[CHECKPOINT] restart: RestartLevel is unavailable\n");
        return;
    }
    fprintf(stderr, "[CHECKPOINT] restart from checkpoint\n");
    call_guest(NULL,0x00124250u,0u,0u,0u,NULL,0u);
}
/* The Saves tab (save_profiles.h): the save manager [0x2D19BC] keeps the
 * profile file's image at +0x1268 (0x1F2F20 marks its slots there) and its
 * autosave slot at +0x524 ("AutoSaveIndex" in its own debug page, 0x2094B0).
 * Edits apply only on the main menu pages, never in a mission. */
#include "save_profiles.h"
static void saves_tick(uint32_t ui)
{
    uint32_t manager = MEM32(0x2D19BC);
    int main_menu = !black_GameInMission() && (black_FlashOnPage(&menu, ui, "blacktitlemenu") ||
        black_FlashOnPage(&menu, ui, "OptionsMenuXbox") || black_FlashOnPage(&menu, ui, "ProfileOptions"));
    uint8_t *image = manager && memory_ok(manager + 0x1268, BLACK_PROFILE_SIZE) ? (uint8_t *)XBOX_PTR(manager + 0x1268) : NULL;
    int slot = manager && memory_ok(manager + 0x524, 4) ? (int)MEM32(manager + 0x524) : -1;
    black_SaveGameTick(image, slot, main_menu);
}
/* The slot's record (slot_info.h): the profile being played, the mission in
 * play and its difficulty, read as the checkpoints read them (the profile at
 * [[0x2D19B4]], the core's level at +0x5AEC, difficulty at main+0x2014C). */
#include "slot_info.h"
#include "mission_stats.h"
static void slot_info_tick(void)
{
    uint32_t manager = MEM32(0x2D19B4), profile = manager && memory_ok(manager, 4) ? MEM32(manager) : 0, key = 0;
    if (profile && memory_ok(profile, 0x6A8) && MEM8(profile + 8) && !MEM8(profile + 1701)) {
        key = 2166136261u ^ MEM32(profile);
        for (uint32_t i = 0; i < 80; ++i) key = (key ^ MEM8(profile + 8 + i)) * 16777619u;
        if (!key) key = 1;
    }
    /* A mission of a loaded profile: at boot the game passes a mission state
     * of stage 0 with no profile, which is not one. */
    int in_mission = black_GameInMission() && key, level = -1, difficulty = -1;
    if (in_mission) {
        uint32_t core = MEM32(0x2D45D4), game = MEM32(0x2D45F0);
        if (core && game && memory_ok(core + 0x5AEC, 2) && memory_ok(game + 0x2014C, 4)) {
            uint32_t l = MEM8(core + 0x5AEC), stage = MEM8(core + 0x5AED), d = MEM32(game + 0x2014C);
            if (!stage) in_mission = 0;
            else if (l <= 8 && l != 2 && d <= 3) { level = (int)l; difficulty = (int)d; }
        }
    }
    int playing = in_mission && !black_GamePaused() && !black_MenuIsOpen();
    black_SlotInfoTick(monotonic(), key, playing, level, difficulty);
    black_StatsTick(monotonic(), in_mission, level, difficulty, playing, black_GamePaused());
}
/* The front end's input queue, read before the page's own script takes it
 * (the overlay calls this at the title's tick, 0x124369): a CONFIRM on the
 * checkpoint item resumes here instead. 5 is the front end's confirm. */
void black_GameCheckpointConfirm(uint32_t ui)
{
    if(!memory_ok(ui+0x44C,4)||!MEM32(ui+0x44C))return;
    uint32_t input=MEM32(ui+0x44C);
    if(menu.cp_button&&input==5&&black_FlashCheckpointConfirm(&menu,ui)){MEM32(ui+0x44C)=0;return;}
    /* Profile options' Checkpoints row: left, right and A are its own. */
    if(menu.cp_row&&black_FlashCheckpointRowInput(&menu,ui,input))MEM32(ui+0x44C)=0;
}
void black_GameMenu(uint32_t ui)
{
    initialize();uint32_t previous_ebp=g_ebp,previous_seh=g_seh_ebp;g_ebp=ui;g_seh_ebp=ui;
    /* The game's own Autosave (Profile options): the checkpoints' gate. */
    { uint32_t game=MEM32(0x2D45F0); if(game&&memory_ok(game+0x20164,1))black_CheckpointSetAutosave(MEM8(game+0x20164)==1); }
    black_FlashMenuTick(&menu,ui,black_GamePointerTake());
    black_FlashCheckpointItem(&menu,ui);
    black_FlashCheckpointRow(&menu,ui);
    quit_to_menu(ui);
    restart_tick();
    saves_tick(ui);
    slot_info_tick();
    black_PromptTick(&prompts);
    g_ebp=previous_ebp;g_seh_ebp=previous_seh;
    if(menu.fault&&getenv("RECOMP_INPUT_DEBUG")){
        static unsigned failures;if(++failures==1)fprintf(stderr,"[FLASH-MENU] invalid guest structure or call; input skipped\n");
    }
}
void black_GameNameEntry(uint32_t ui)
{
    initialize();uint32_t previous_ebp=g_ebp,previous_seh=g_seh_ebp;g_ebp=ui;g_seh_ebp=ui;
    black_FlashNameTick(&menu,ui);
    g_ebp=previous_ebp;g_seh_ebp=previous_seh;
}

/* The front end's string id of a token (its name without '$'): the title's
 * own table hash, as TGP's options_menu.cpp computes it. */
static uint32_t token_id(const char *name)
{
    uint32_t h=~0u;
    for(;*name;++name)h=(uint32_t)((int32_t)h>>8)^MEM32(0x2AC908u+((h^(uint8_t)*name)&0xFFu)*4u);
    return h;
}
/* $BLACK_CHECKPOINTS, the Profile options' Checkpoints row (flash_menu.c), in
 * the game's language: UTF-8, as the Russian bank is built (game_translation.h),
 * whose Cyrillic the run's fonts carry at its own code points. */
static uint32_t checkpoints_label(void)
{
    static const char *const label[]={"CHECKPOINTS","POINTS DE CONTROLE","CHECKPOINTS","PUNTOS DE CONTROL","CHECKPOINT","ЧЕКПОИНТЫ"};
    static uint32_t text;
    if(!text){
        int language=black_SettingsGameLanguage();
        if(language<0||language>5)language=0;
        size_t length=strlen(label[language])+1;
        text=alloc_guest(NULL,length);
        if(text)memcpy((void *)XBOX_PTR(text),label[language],length);
    }
    return text;
}
extern void black_OriginalString(void);
void sub_001F3A80(void)
{
    initialize();
    uint32_t id = memory_ok(g_esp+4,4) ? MEM32(g_esp+4) : 0;
    static uint32_t checkpoints_id;
    if(!checkpoints_id)checkpoints_id=token_id("BLACK_CHECKPOINTS");
    black_OriginalString();
    if(id&&id==checkpoints_id){uint32_t text=checkpoints_label();if(text){g_eax=text;return;}}
    static pthread_mutex_t strings_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&strings_lock);
    g_eax = black_PromptString(&prompts,id,g_eax);
    pthread_mutex_unlock(&strings_lock);
}

extern void black_OriginalHintFormat(void), black_OriginalHintTick(void);
void sub_00113BF0(void)
{
    initialize();
    uint32_t caller=memory_ok(g_esp,8)?MEM32(g_esp):0;
    uint32_t buffer=memory_ok(g_esp,8)?MEM32(g_esp+4):0;
    if (black_HudPromptFormat(&hud_prompts,&prompts.api,caller,buffer,g_ebx)) {
        g_esp+=4; /* cdecl void formatter(buffer): caller removes its argument */
    } else black_OriginalHintFormat();
}
void sub_001314E0(void)
{
    initialize();
    black_HudPromptTick(&hud_prompts,&prompts.api,g_ecx);
    black_OriginalHintTick();
}
void black_GamePickupText(uint32_t hud)
{ initialize(); black_HudPickupReady(&hud_prompts,&prompts.api,hud); }
