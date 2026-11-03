/* Compile the production native guest-call adapter with a destructive callee.
 * Verify all virtual registers and stack argument order, not a parallel adapter. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/macos/overrides/flash_menu.h"
#include "../../ports/macos/overrides/hud_prompts.h"
#include "../../ports/macos/overrides/game_prompts.h"
typedef union {uint64_t q[2];} RecompXmm;
typedef union {uint64_t q;} RecompMmx;
typedef void (*recomp_func_t)(void);
static struct Cpu {uint32_t r[10];int df,top,cmp;uint16_t control,cc;double fp[8];RecompXmm sse[8];RecompMmx mm[8];} cpu;
#define g_eax cpu.r[0]
#define g_ecx cpu.r[1]
#define g_edx cpu.r[2]
#define g_esp cpu.r[3]
#define g_ebx cpu.r[4]
#define g_esi cpu.r[5]
#define g_edi cpu.r[6]
#define g_ebp cpu.r[7]
#define g_seh_ebp cpu.r[8]
#define g_fs_base cpu.r[9]
static uint32_t mxcsr=0x12345678;
#define g_mxcsr mxcsr
#define g_df cpu.df
#define g_fp_top cpu.top
#define g_fp_cmp cpu.cmp
#define g_fp_control_word cpu.control
#define g_fp_cc cpu.cc
#define g_fp_stack cpu.fp
#define g_xmm0 cpu.sse[0]
#define g_xmm1 cpu.sse[1]
#define g_xmm2 cpu.sse[2]
#define g_xmm3 cpu.sse[3]
#define g_xmm4 cpu.sse[4]
#define g_xmm5 cpu.sse[5]
#define g_xmm6 cpu.sse[6]
#define g_xmm7 cpu.sse[7]
#define g_mm0 cpu.mm[0]
#define g_mm1 cpu.mm[1]
#define g_mm2 cpu.mm[2]
#define g_mm3 cpu.mm[3]
#define g_mm4 cpu.mm[4]
#define g_mm5 cpu.mm[5]
#define g_mm6 cpu.mm[6]
#define g_mm7 cpu.mm[7]
static uint32_t ram[0x10000];static int bad_args;
#define MEM32(at) ram[(at)/4]
static BlackFlashMenu menu;
static int memory_ok(uint32_t at,size_t size){return at>=0x10000&&(uint64_t)at+size<=sizeof ram;}
static void destroy(void)
{
    bad_args=g_eax!=11||g_ecx!=22||g_edx!=33||MEM32(g_esp)!=0x124374||MEM32(g_esp+4)!=44||MEM32(g_esp+8)!=55;
    memset(&cpu,0xcc,sizeof cpu);mxcsr=0xcccccccc;g_eax=0xcafebabe;
}
static recomp_func_t recomp_lookup_manual(uint32_t at){(void)at;return NULL;}
static recomp_func_t recomp_lookup(uint32_t at){return at==0x123?destroy:NULL;}
#define RECOMP_ICALL_IS_CODE(at) ((at)==0x123)
#include "../../ports/macos/overrides/flash_guest_call.inc"
static BlackGamePrompts prompts;
static BlackHudPrompts hud_prompts;
static int handled, original_calls, handler_bad;
static void initialize(void) {}
int black_HudPromptFormat(BlackHudPrompts *h,const BlackFlashApi *a,uint32_t caller,uint32_t buffer,uint32_t hud)
{
    handler_bad=h!=&hud_prompts || a!=&prompts.api || caller!=0x12B4EF || buffer!=0x18000 || hud!=0x16000;
    return handled;
}
void black_OriginalHintFormat(void) { ++original_calls;g_esp+=4; }
#include BLACK_HINT_WRAPPER_SOURCE
int main(void)
{
    memset(&cpu,0x5a,sizeof cpu);g_esp=0x30000;struct Cpu before=cpu;uint32_t args[]={44,55};
    uint32_t value=call_guest(NULL,0x123,11,22,33,args,2);
    if(bad_args||mxcsr!=0x12345678||value!=0xcafebabe||memcmp(&before,&cpu,sizeof cpu)||menu.fault)return 1;
    if(call_guest(NULL,0x124,0,0,0,NULL,0)||!menu.fault||memcmp(&before,&cpu,sizeof cpu))return 2;
    menu.fault=0;g_esp=0x10000;before=cpu;
    if(call_guest(NULL,0x123,0,0,0,args,2)||!menu.fault||memcmp(&before,&cpu,sizeof cpu))return 3;
    for(handled=0;handled<2;++handled) {
        g_esp=0x20000;g_ebx=0x16000;MEM32(g_esp)=0x12B4EF;MEM32(g_esp+4)=0x18000;
        before=cpu;before.r[3]+=4;original_calls=0;handler_bad=0;
        sub_00113BF0();
        if(handler_bad || original_calls!=!handled || memcmp(&before,&cpu,sizeof cpu)) return 4;
        if(MEM32(0x20004)!=0x18000)return 5; /* cdecl caller still owns its argument */
    }
    puts("Production Flash guest call: args/return, GPR/stack/SEH/TIB/x87/MMX/SSE/control restoration, unresolved/stack guards and cdecl hint-wrapper stack: PASS");return 0;
}
