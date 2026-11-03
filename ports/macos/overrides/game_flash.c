/* Guest calls in the pinned runtime's native-C ABI. Restore every virtual
 * register, including x87/MMX/SSE/SEH, after each Flash method invocation. */
/* MMX declarations in the pinned header are inside this guard. */
#define RECOMP_GENERATED_CODE 1
#include "recomp_types.h"
#include "flash_menu.h"
#include "flash_memory.h"
#include "game_prompts.h"
#include "hud_prompts.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
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
static pthread_once_t api_once = PTHREAD_ONCE_INIT;
static void initialize_api(void)
{ menu.api=(BlackFlashApi){NULL,read_guest,write_guest,call_guest,alloc_guest,trace_guest,getenv("RECOMP_INPUT_DEBUG")!=NULL}; prompts.api=menu.api; }
static void initialize(void) { pthread_once(&api_once,initialize_api); }
void black_GameMenu(uint32_t ui)
{
    initialize();uint32_t previous_ebp=g_ebp,previous_seh=g_seh_ebp;g_ebp=ui;g_seh_ebp=ui;
    black_FlashMenuTick(&menu,ui,black_GamePointerTake());
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

extern void black_OriginalString(void);
void sub_001F3A80(void)
{
    initialize();
    uint32_t id = memory_ok(g_esp+4,4) ? MEM32(g_esp+4) : 0;
    black_OriginalString();
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
