#include "../../ports/macos/overrides/game_prompts.h"
#include "../../ports/macos/overrides/input_profile.h"
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned char mem[0x400000];
static uint32_t heap=0x300000;
static int resets;
static int read_(void *o,uint32_t at,void *out,size_t n)
{ (void)o; if(!at || at+n>sizeof mem || at+n<at)return 0;memcpy(out,mem+at,n);return 1; }
static int write_(void *o,uint32_t at,const void *in,size_t n)
{ (void)o; if(!at || at+n>sizeof mem || at+n<at)return 0;memcpy(mem+at,in,n);return 1; }
static uint32_t alloc_(void *o,size_t n)
{ (void)o;uint32_t at=heap;if(heap+n>sizeof mem)return 0;heap+=(uint32_t)n;return at; }
static uint32_t call_(void *o,uint32_t target,uint32_t a,uint32_t c,uint32_t d,const uint32_t *args,size_t n)
{ (void)o;(void)a;(void)c;(void)d;(void)args;(void)n;assert(target==0xECA90);++resets;return 0; }
static void w(uint32_t at,uint32_t n){assert(write_(NULL,at,&n,4));}
static uint32_t r(uint32_t at){uint32_t n=0;assert(read_(NULL,at,&n,4));return n;}
static void children(uint32_t parent,uint32_t child,uint32_t data)
{ w(parent+0x48,data);w(data+0x24,data+0x100);w(data+0x100,data+0x200);w(data+0x200+0x50,child); }
int main(void)
{
    setenv("BLACK_PROMPT_TEXT_SYMBOLS","1",1);
    char folder[]="/tmp/black-prompts-XXXXXX",path[512];assert(mkdtemp(folder));
    snprintf(path,sizeof path,"%s/settings.ini",folder);setenv("BLACK_SETTINGS_FILE",path,1);
    BlackGamePrompts p={.api={NULL,read_,write_,call_,alloc_,NULL,0}};
    strcpy((char *)mem+0x20000,"confirm");strcpy((char *)mem+0x21000,"PRESS ² :");
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_XBOX);
    assert(black_PromptString(&p,0xE0CA7C5F,0x20000)==0x20000);
    assert(black_PromptString(&p,0x53773143,0x21000)==0x21000);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_PLAYSTATION);
    uint32_t text=black_PromptString(&p,0xE0CA7C5F,0x20000);assert(!strcmp((char *)mem+text,"CROSS confirm"));
    /* Name keyboard: B backs out there, so the pad deletes with X and types a space with Y. */
    strcpy((char *)mem+0x22000,"delete");strcpy((char *)mem+0x23000,"space");
    text=black_PromptString(&p,0x32D594BD,0x22000);assert(!strcmp((char *)mem+text,"SQUARE delete"));
    text=black_PromptString(&p,0x758E2074,0x23000);assert(!strcmp((char *)mem+text,"TRIANGLE space"));
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_XBOX);
    assert(black_PromptString(&p,0x32D594BD,0x22000)!=0x22000);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_KEYBOARD);
    text=black_PromptString(&p,0x32D594BD,0x22000);assert(!strcmp((char *)mem+text,"BACKSPACE delete"));
    text=black_PromptString(&p,0x758E2074,0x23000);assert(!strcmp((char *)mem+text,"SPACE space"));
    text=black_PromptString(&p,0xE0CA7C5F,0x20000);assert(!strcmp((char *)mem+text,"ENTER confirm"));
    text=black_PromptString(&p,0x53773143,0x21000);assert(!strcmp((char *)mem+text,"PRESS R :"));
    black_SettingsSetBinding(9,0,18);
    assert(black_PromptString(&p,0x53773143,0x21000)==text);assert(!strcmp((char *)mem+text,"PRESS 1 :"));
    black_SettingsSetBinding(9,0,-1);black_SettingsSetBinding(9,1,-1);
    black_PromptString(&p,0x53773143,0x21000);assert(!strcmp((char *)mem+text,"PRESS UNBOUND :"));
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_PLAYSTATION);black_SettingsSetPadBinding(0,2);
    black_PromptString(&p,0x53773143,0x21000);assert(!strcmp((char *)mem+text,"PRESS SQUARE :"));
    unsetenv("BLACK_PROMPT_TEXT_SYMBOLS");
    char icon[8];black_InputGlyphUTF8(0xE112,icon,sizeof icon);
    text=black_PromptString(&p,0x53773143,0x21000);assert(strstr((char *)mem+text,icon));
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_KEYBOARD);
    black_InputGlyphUTF8(0xE200+36,icon,sizeof icon);
    text=black_PromptString(&p,0xE0CA7C5F,0x20000);assert(strstr((char *)mem+text,icon));
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_PLAYSTATION);
    assert(black_PromptString(&p,42,0xFFFFFFFC)==0xFFFFFFFC);
    /* Real offsets, bounded Flash graph, visibility/position restored once. */
    w(0x2EC988,0x1000);w(0x1000+0x18,0x1100);w(0x1100,0x1200);
    w(0x1200+4,0x12u<<25|16);w(0x1200+0x54,4);
    w(0x1300+4,0xDu<<25|16);children(0x1200,0x1300,0x5000);
    children(0x1300,0x1400,0x6000);w(0x1400+0x50,0x1500);
    w(0x1400+4,0xDu<<25|16);w(0x1500+4,0xFu<<25|16);
    w(0x1400+8,0x7000);w(0x1500+8,0x7100);
    strcpy((char *)mem+0x7008,"Button");strcpy((char *)mem+0x7108,"Text");
    w(0x1400+0x58,0x100000);float x=100;memcpy(mem+0x1500+0x1C,&x,4);
    w(0x2ED408,1);
    black_PromptTick(&p);assert(!(r(0x1400+0x58)&0x100000));
    memcpy(&x,mem+0x1500+0x1C,4);assert(x==72);assert(resets==0);
    black_PromptTick(&p);memcpy(&x,mem+0x1500+0x1C,4);assert(x==72 && resets==0);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_XBOX);black_SettingsSetPadBinding(0,0);
    black_PromptTick(&p);memcpy(&x,mem+0x1500+0x1C,4);assert(x==100 && resets==1);
    assert(r(0x1400+0x58)&0x100000);
    black_SettingsSetBinding(9,0,15);black_PromptTick(&p);assert(resets==2);
    /* A cyclic clip list exhausts the budget, without recursion/hang. */
    w(0x1400+0x50,0x1400);black_PromptTick(&p);assert(p.visits<=4096);
    unlink(path);rmdir(folder);puts("Guest prompts, rebind refresh, glyph fallback, Flash restoration/bounds: PASS");
}
