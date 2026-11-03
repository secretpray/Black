#include "../../ports/macos/overrides/hud_prompts.h"
#include "../../ports/macos/overrides/input_profile.h"
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned char mem[0x400000];
static uint32_t heap=0x300000;
static int corrupt,oversize,calls;
static int read_(void *o,uint32_t at,void *out,size_t n)
{(void)o;if(!at || at+n<at || at+n>sizeof mem)return 0;memcpy(out,mem+at,n);return 1;}
static int write_(void *o,uint32_t at,const void *in,size_t n)
{(void)o;if(!at || at+n<at || at+n>sizeof mem)return 0;memcpy(mem+at,in,n);return 1;}
static uint32_t alloc_(void *o,size_t n)
{(void)o;uint32_t at=heap;heap+=(uint32_t)n;assert(heap<sizeof mem);return at;}
static void w(uint32_t at,uint32_t n){assert(write_(NULL,at,&n,4));}
static void wide(uint32_t at,const char *s)
{uint16_t out[200]={0};for(unsigned i=0;s[i];++i)out[i]=(unsigned char)s[i];assert(write_(NULL,at,out,sizeof out));}
static int is(uint32_t at,const char *s)
{for(unsigned i=0;;++i){uint16_t c;assert(read_(NULL,at+i*2,&c,2));if(c!=(unsigned char)s[i])return 0;if(!c)return 1;}}
static uint32_t call_(void *o,uint32_t target,uint32_t a,uint32_t c,uint32_t d,const uint32_t *args,size_t n)
{
    (void)o;(void)a;(void)c;(void)d;assert(target==0x113BF0 && n==1);++calls;
    if(oversize){uint16_t text[300];for(int i=0;i<299;++i)text[i]='X';text[299]=0;write_(NULL,args[0],text,sizeof text);}
    else {
        int confirm=black_PromptSetConfirmContext(0);black_PromptSetConfirmContext(confirm);
        char label[32];black_PromptButtonLabel(0,confirm,label,sizeof label);wide(args[0],label);
    }
    if(corrupt)mem[args[0]-1]=0;
    return 0;
}
int main(void)
{
    setenv("BLACK_PROMPT_TEXT_SYMBOLS","1",1);
    char folder[]="/tmp/black-hud-XXXXXX",path[512];assert(mkdtemp(folder));
    snprintf(path,sizeof path,"%s/settings.ini",folder);setenv("BLACK_SETTINGS_FILE",path,1);
    BlackFlashApi api={NULL,read_,write_,call_,alloc_,NULL,0};BlackHudPrompts h={0};uint32_t hud=0x10000;
    w(0x2D12E4,0x2000);w(0x200C,hud);w(hud+0x22A4,42);w(hud+0x2110,0xFFFFFFFF);
    w(0x2F3BD4,0x3000);w(0x3004,16);w(0x2AC290+16*4,0x4000);strcpy((char *)mem+0x4000,"BUTTON_A");
    uint16_t raw[]={0xF001,0};write_(NULL,hud+0x2114,raw,sizeof raw);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_KEYBOARD);
    assert(!black_HudPromptFormat(&h,&api,0x123456,hud+0x2114,hud));assert(!calls);
    assert(black_HudPromptFormat(&h,&api,0x12B4EF,hud+0x2114,hud));assert(is(hud+0x2114,"R"));
    write_(NULL,hud+0x22C0,raw,sizeof raw);assert(black_HudPromptFormat(&h,&api,0x12B4E0,hud+0x22C0,hud));
    assert(is(hud+0x22C0,"ENTER")); /* continue is confirm, not reload */
    black_SettingsSetBinding(9,0,18);black_HudPromptTick(&h,&api,hud);assert(is(hud+0x2114,"1"));
    /* The title moves the pending text into the active buffer. */
    memcpy(mem+hud+0x1F80,mem+hud+0x2114,400);w(hud+0x2110,42);w(hud+0x22A4,0xFFFFFFFF);mem[hud+0x1B]=5;
    black_HudPromptTick(&h,&api,hud);assert(h.active.body.valid && !h.pending.body.valid);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_XBOX);black_HudPromptTick(&h,&api,hud);assert(is(hud+0x1F80,"A"));
    /* A recycled/edited buffer must never be overwritten with cached text. */
    wide(hud+0x1F80,"NEW MESSAGE");black_SettingsSetButtonPrompts(BLACK_PROMPTS_KEYBOARD);
    black_HudPromptTick(&h,&api,hud);assert(is(hud+0x1F80,"NEW MESSAGE") && !h.active.body.valid);
    /* Oversized expansion or clobbered guard cannot touch a 200-unit caption. */
    w(hud+0x22A4,43);write_(NULL,hud+0x2114,raw,sizeof raw);oversize=1;
    assert(black_HudPromptFormat(&h,&api,0x12B4EF,hud+0x2114,hud));assert(!h.pending.body.valid);
    assert(mem[ hud+0x2114 ]==1 && mem[hud+0x2115]==0xF0);oversize=0;corrupt=1;
    black_HudPromptFormat(&h,&api,0x12B4EF,hud+0x2114,hud);assert(!h.pending.body.valid);corrupt=0;
    uint16_t invalid[]={0xF026,0};write_(NULL,hud+0x2114,invalid,sizeof invalid);int before=calls;
    black_HudPromptFormat(&h,&api,0x12B4EF,hud+0x2114,hud);assert(calls==before);
    /* Pickup native face glyph, weapon identity/detail, live rebind, restoration. */
    w(0x3000+13*4,18);w(0x2A7DD4+3*4,18);w(0x2AB72C+3*4,0x4100);mem[0x4100]=0xB3;
    uint16_t pickup[]={ 'H','O','L','D',' ',0xB3,0 };write_(NULL,hud+0x27F0,pickup,sizeof pickup);
    wide(hud+0x2980,"AK47");mem[hud+0x2B10]=1;mem[hud+0x2B13]=0;mem[hud+0x1C]=2;mem[hud+0x1D]=3;
    black_HudPickupReady(&h,&api,hud);assert(is(hud+0x27F0,"HOLD F"));
    black_SettingsSetBinding(10,0,18);black_HudPromptTick(&h,&api,hud);assert(is(hud+0x27F0,"HOLD 1"));
    unsetenv("BLACK_PROMPT_TEXT_SYMBOLS");
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_PLAYSTATION);black_HudPromptTick(&h,&api,hud);
    uint16_t square;read_(NULL,hud+0x27F0+10,&square,2);assert(square==0xE112);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_XBOX);black_HudPromptTick(&h,&api,hud);assert(!memcmp(mem+hud+0x27F0,pickup,sizeof pickup));
    wide(hud+0x2980,"NEW WEAPON");black_SettingsSetButtonPrompts(BLACK_PROMPTS_KEYBOARD);
    black_HudPromptTick(&h,&api,hud);assert(!h.pickup.valid && !memcmp(mem+hud+0x27F0,pickup,sizeof pickup));
    /* Wrong owner and a new HUD discard cached fields. */
    w(0x200C,hud+0x4000);black_HudPromptTick(&h,&api,hud+0x4000);assert(!h.active.body.valid);
    unlink(path);rmdir(folder);puts("Cached HUD prompts: pending/active, rebind, confirm, bounds, pickup and owner guards: PASS");
}
