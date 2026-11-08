/* Native-C adaptation of TGP game/prompts.cpp at 10b8464fa06e.
 * Reads and writes are checked by the same API as our Flash cursor.
 * No C++ guest context or generated source ABI crosses this boundary. */
#include "game_prompts.h"
#include "input_profile.h"
#include "keyboard_mouse.h"
#include "hud_prompts.h"
#include "port_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
static _Thread_local int prompt_confirm;
int black_PromptSetConfirmContext(int confirm)
{ int previous=prompt_confirm;prompt_confirm=confirm;return previous; }
enum { TEXT_CAP = 1024, SHOWN = 0x100000, PLACED = 0x10000 };
static uint32_t r32(BlackGamePrompts *p, uint32_t at)
{ uint32_t n = 0; if (at) p->api.read(p->api.opaque,at,&n,4); return n; }
static void w32(BlackGamePrompts *p, uint32_t at, uint32_t n)
{ if (at) p->api.write(p->api.opaque,at,&n,4); }
static int string(BlackGamePrompts *p, uint32_t at, char *out, size_t cap)
{
    if (!at) return 0;
    for (size_t i = 0; i < cap; ++i) {
        if (!p->api.read(p->api.opaque,at+(uint32_t)i,out+i,1)) return 0;
        if (!out[i]) return 1;
    }
    return 0;
}
void black_PromptButtonLabel(int target,int menu,char *out,size_t cap)
{
    if (!getenv("BLACK_PROMPT_TEXT_SYMBOLS")) {
        black_InputGlyphUTF8(black_InputButtonGlyph(target,menu),out,cap); return;
    }
    black_InputButtonLabel(target,menu,out,cap);
    if (getenv("BLACK_PROMPT_TEXT_SYMBOLS")) {
        const char *symbols[] = {"×","○","□","△"}, *names[] = {"CROSS","CIRCLE","SQUARE","TRIANGLE"};
        for (int i=0;i<4;++i) if (!strcmp(out,symbols[i])) snprintf(out,cap,"%s",names[i]);
    }
}
static const uint32_t button_ids[16] = {0x53773143,0xCA7E60F9,0x371C9983,0x401BA915,0x1C8B6988,0xD7C11E04,
    0xDDC13733,0xBCBBFDF2,0xBB8906E1,0xCA47AFA2,0x53DFD8F1,0xD4A32971,0x0A88862F,0xDC57E320,0x8D194B52,0xB900C989};
static int replace(char *out,size_t cap,const char *from,const char *to)
{
    size_t a=strlen(from), b=strlen(to); int changed=0;
    char *at=out;
    while ((at=strstr(at,from))) {
        size_t tail=strlen(at+a);
        if ((size_t)(at-out)+b+tail+1>cap) return changed;
        memmove(at+b,at+a,tail+1); memcpy(at,to,b); at+=b; changed=1;
    }
    return changed;
}
uint32_t black_PromptString(BlackGamePrompts *p,uint32_t id,uint32_t original)
{
    if (!original) return original;
    char before[TEXT_CAP], out[TEXT_CAP], key[32];
    if (!string(p,original,before,sizeof before)) return original;
    memcpy(out,before,strlen(before)+1);
    int family=black_InputPromptFamily(), changed=0;
    for (int i=0;i<16;++i) if (button_ids[i]==id) {
        if (family==BLACK_PROMPTS_XBOX && black_SettingsPadBinding(i)==i) return original;
        black_PromptButtonLabel(i,prompt_confirm,key,sizeof key);
        static const char *press[]={"PRESS","APPUYEZ","DRÜCKEN","PULSA","PREMI","НАЖМИТЕ"};
        int language=black_SettingsGameLanguage();
        /* The title expands these strings into a 32-byte hint field. */
        snprintf(out,32,"%s %s :",press[language],key); changed=1; break;
    }
    if (id==0xE0CA7C5F || id==0x64199B75 || id==0x32D594BD) {
        /* The name keyboard's DELETE key: B backs out of that page, so the
         * pad deletes with X (text_pad.h) and the keyboard with Backspace. */
        if (id==0x32D594BD && family!=BLACK_PROMPTS_KEYBOARD) {
            black_PromptButtonLabel(2,1,key,sizeof key);
            snprintf(out,sizeof out,"%s %.960s",key,before); changed=1;
        } else if (family!=BLACK_PROMPTS_XBOX) {
            black_PromptButtonLabel(id==0xE0CA7C5F?0:1,1,key,sizeof key);
            if (id==0x32D594BD && family==BLACK_PROMPTS_KEYBOARD) {
                if(getenv("BLACK_PROMPT_TEXT_SYMBOLS")) snprintf(key,sizeof key,"BACKSPACE");
                else black_InputGlyphUTF8(0xE200+51,key,sizeof key);
            }
            snprintf(out,sizeof out,"%s %.960s",key,before); changed=1;
        }
    } else if (id==0x758E2074) {
        /* The name keyboard's SPACE key: Y on the pad (text_pad.h), Space on the keyboard. */
        if (family==BLACK_PROMPTS_KEYBOARD) {
            if(getenv("BLACK_PROMPT_TEXT_SYMBOLS")) snprintf(key,sizeof key,"SPACE");
            else black_InputGlyphUTF8(0xE200+49,key,sizeof key);
        } else black_PromptButtonLabel(3,1,key,sizeof key);
        snprintf(out,sizeof out,"%s %.960s",key,before); changed=1;
    } else if (id==0x2FCCE936 || id==0x905E4EE0 || id==0x661A3A37) {
        /* PRESS START keeps the game's own words in its language: START
         * means the controller's start button on any device, while an icon
         * (OPTIONS on a DualSense) read as an unknown button there. */
        return original;
    } else if (!changed && family!=BLACK_PROMPTS_XBOX) {
        /* Embedded face glyphs in control/help captions, as in TGP. */
        static const char *glyphs[]={"²","¹","³","°"};
        for (int i=0;i<4;++i) {
            black_PromptButtonLabel(i,black_GameMenuActive(),key,sizeof key);
            changed|=replace(out,sizeof out,glyphs[i],key);
        }
    }
    if (!changed || !strcmp(before,out)) return original;
    unsigned index=0;
    while (index<p->count && p->texts[index].id!=id) ++index;
    if (index==p->count) {
        if (p->count>=128) return original;
        uint32_t address=p->api.alloc(p->api.opaque,TEXT_CAP);
        if (!address) return original;
        p->texts[index]=(BlackPromptText){id,address}; ++p->count;
    }
    return p->api.write(p->api.opaque,p->texts[index].address,out,strlen(out)+1) ? p->texts[index].address : original;
}
static unsigned type(BlackGamePrompts *p,uint32_t at)
{ uint32_t flags=r32(p,at+4); return at && (flags&16) ? flags>>25 : 0; }
static uint32_t first(BlackGamePrompts *p,uint32_t clip)
{
    unsigned t=type(p,clip); if (t!=0xD && t!=0x12) return 0;
    uint32_t data=r32(p,clip+0x48);
    if (!data || data==0xBAADF00D) return 0;
    uint32_t list=r32(p,data+0x24), head=list?r32(p,list):0;
    return head?r32(p,head+0x50):0;
}
static void icons(BlackGamePrompts *p,uint32_t clip,int custom,int depth)
{
    for (uint32_t child=first(p,clip);child && p->visits<4096;child=r32(p,child+0x50)) {
        ++p->visits;
        uint32_t icon=0,text=0;
        for (uint32_t part=first(p,child);part && p->visits<4096;part=r32(p,part+0x50)) {
            ++p->visits;
            char name[64]; unsigned t=type(p,part);
            if (t<0xC || t>0x12 || !string(p,r32(p,part+8)+8,name,sizeof name)) continue;
            if (!strcasecmp(name,"Button")) icon=part;
            if (!strcasecmp(name,"Text")) text=part;
        }
        if (!icon || !text) { if (depth<8) icons(p,child,custom,depth+1); continue; }
        uint32_t flags=r32(p,icon+0x58);
        if (!!(flags&SHOWN)!=custom) continue;
        uint32_t bits=r32(p,text+0x1C); float x; memcpy(&x,&bits,4);
        if (!isfinite(x)) continue;
        x+=custom?-28:28; memcpy(&bits,&x,4);
        w32(p,icon+0x58,flags^SHOWN); w32(p,text+0x1C,bits);
        w32(p,text+0x58,r32(p,text+0x58)|PLACED);
    }
}
uint64_t black_PromptSignature(void)
{
    uint64_t signature=(unsigned)black_InputPromptFamily()+1;
    for(int i=0;i<BLACK_ACTION_COUNT;++i) for(int j=0;j<2;++j) signature=signature*1099511628211ull+(unsigned)(black_SettingsBinding(i,j)+1);
    for(int i=0;i<16;++i) signature=signature*1099511628211ull+(unsigned)(black_SettingsPadBinding(i)+1);
    return signature;
}
void black_PromptTick(BlackGamePrompts *p)
{
    uint64_t signature=black_PromptSignature();
    uint32_t player=r32(p,0x2EC988);
    if (signature!=p->signature) {
        if (p->initialized && r32(p,0x2ED408)==1 && player)
            p->api.call(p->api.opaque,0xECA90,0,0,0,NULL,0);
        fprintf(stderr,"[PROMPTS] family=%d bindings refreshed\n",black_InputPromptFamily());
        p->signature=signature; p->initialized=1;
    }
    uint32_t movies=player?r32(p,player+0x18):0, movie=movies?r32(p,movies):0;
    p->visits=0;
    for(int i=0;movie && i<256;++i,movie=r32(p,movie+0x50)) {
        int depth=(int32_t)(r32(p,movie+0x54)<<15)>>15;
        if(depth==4 && type(p,movie)==0x12) { icons(p,movie,black_InputPromptFamily()!=BLACK_PROMPTS_XBOX,0); break; }
    }
}
