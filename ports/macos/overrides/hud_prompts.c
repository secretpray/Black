/* HUD ownership, offsets and pending -> active handoff follow portingpete's
 * black_hud_input.inc at 2041e5a97f13 (MIT). Native-C callbacks replace its
 * Windows context/SEH; bounds, exact rendered-text matching and UTF-16 caps
 * protect recycled HUD objects and captions. No guessed arbitrary-text edits. */
#include "hud_prompts.h"
#include "input_profile.h"
#include <string.h>
enum { SCRATCH_UNITS = 6400, GUARD = 32 };
static uint32_t r32(const BlackFlashApi *a,uint32_t at)
{ uint32_t n=0; if(at) a->read(a->opaque,at,&n,4);return n; }
static uint8_t r8(const BlackFlashApi *a,uint32_t at)
{ uint8_t n=0; if(at) a->read(a->opaque,at,&n,1);return n; }
static unsigned length(const uint16_t *s,unsigned cap)
{ unsigned n=0;while(n<cap && s[n]) ++n;return n; }
static int copy(const BlackFlashApi *a,uint32_t at,uint16_t *out,unsigned cap)
{ memset(out,0,BLACK_HINT_UNITS*2);return a->read(a->opaque,at,out,cap*2) && length(out,cap)<cap; }
static int matches(const BlackFlashApi *a,uint32_t at,const uint16_t *expected,unsigned cap)
{ uint16_t actual[BLACK_HINT_UNITS];return copy(a,at,actual,cap) && !memcmp(actual,expected,(length(expected,cap)+1)*2); }
static int current(BlackHudPrompts *h,const BlackFlashApi *a,uint32_t hud)
{
    uint32_t owner=r32(a,0x2D12E4);uint8_t end;
    if(!owner || r32(a,owner+12)!=hud || !hud || !a->read(a->opaque,hud+0x2B1F,&end,1)) return 0;
    if(h->owner!=owner || h->hud!=hud) {
        h->owner=owner;h->hud=hud;
        memset(&h->pending,0,sizeof h->pending);memset(&h->active,0,sizeof h->active);memset(&h->pickup,0,sizeof h->pickup);
    }
    return 1;
}
static int tokens(const BlackFlashApi *a,const uint16_t *raw,unsigned cap)
{
    for(unsigned i=0;i<cap;++i) {
        unsigned c=raw[i];if(!c)return 1;if(c<0xF001)continue;if(c>0xF025)return 0;
        uint32_t map=r32(a,0x2F3BD4), control=c==0xF025?16:r32(a,map+(c-0xF000)*4);
        if((c!=0xF025 && !map) || control>=28)return 0;
        uint32_t name=r32(a,0x2AC290+control*4);int terminated=0;
        for(unsigned j=0;name && j<64;++j) { uint8_t byte;
            if(!a->read(a->opaque,name+j,&byte,1) || byte>=128)return 0;
            if(!byte){terminated=1;break;}
        }
        if(!terminated)return 0;
    }
    return 0;
}
static int format(BlackHudPrompts *h,const BlackFlashApi *a,BlackHudPromptField *field,uint16_t *out)
{
    if(!tokens(a,field->raw,field->capacity))return 0;
    if(!h->scratch)h->scratch=a->alloc(a->opaque,GUARD+SCRATCH_UNITS*2+GUARD);
    if(!h->scratch)return 0;
    uint16_t text[SCRATCH_UNITS]={0};uint8_t guard[GUARD],check[GUARD];memset(guard,0xA5,sizeof guard);
    memcpy(text,field->raw,field->capacity*2);
    uint32_t buffer=h->scratch+GUARD;
    if(!a->write(a->opaque,h->scratch,guard,GUARD) || !a->write(a->opaque,buffer,text,sizeof text) ||
        !a->write(a->opaque,buffer+sizeof text,guard,GUARD))return 0;
    int previous=black_PromptSetConfirmContext(field->confirm);
    a->call(a->opaque,0x113BF0,0,0,0,&buffer,1);
    black_PromptSetConfirmContext(previous);
    if(!a->read(a->opaque,h->scratch,check,GUARD) || memcmp(guard,check,GUARD) ||
        !a->read(a->opaque,buffer+sizeof text,check,GUARD) || memcmp(guard,check,GUARD))return 0;
    return copy(a,buffer,out,field->capacity);
}
int black_HudPromptFormat(BlackHudPrompts *h,const BlackFlashApi *a,uint32_t caller,uint32_t buffer,uint32_t hud)
{
    unsigned cap=caller==0x12B4EF?200:caller==0x12B4E0?64:0;
    if(!cap || !current(h,a,hud) || buffer!=hud+(cap==200?0x2114:0x22C0))return 0;
    uint32_t id=r32(a,hud+0x22A4);if(id==0xFFFFFFFF)return 0;
    if(h->pending.id!=id){memset(&h->pending,0,sizeof h->pending);h->pending.id=id;}
    BlackHudPromptField *f=cap==200?&h->pending.body:&h->pending.continuation;
    memset(f,0,sizeof *f);f->capacity=cap;f->confirm=cap==64;
    if(!copy(a,buffer,f->raw,cap))return 1;
    uint64_t signature=black_PromptSignature();
    uint16_t out[BLACK_HINT_UNITS];
    if(!format(h,a,f,out))return 1; /* No unsafe in-place expansion on failure. */
    if(a->write(a->opaque,buffer,out,(length(out,cap)+1)*2)) {
        memcpy(f->rendered,out,sizeof out);f->valid=1;f->signature=signature;
    }
    return 1;
}
static void refresh(BlackHudPrompts *h,const BlackFlashApi *a,BlackHudPromptField *f,uint32_t buffer,uint64_t signature)
{
    if(!f->valid || f->signature==signature)return;
    if(!matches(a,buffer,f->rendered,f->capacity)){f->valid=0;return;}
    uint16_t out[BLACK_HINT_UNITS];
    if(format(h,a,f,out) && matches(a,buffer,f->rendered,f->capacity) &&
        a->write(a->opaque,buffer,out,(length(out,f->capacity)+1)*2)) {
        memcpy(f->rendered,out,sizeof out);f->signature=signature;
    }
}
/* Names contain only BMP characters: ASCII key names or the four PS symbols. */
static unsigned units(const char *s,uint16_t out[32])
{
    unsigned n=0;const unsigned char *p=(const unsigned char *)s;
    while(*p && n<31){uint32_t c=*p++;
        if(c>=0xE0){if(!p[0]||!p[1])return 0;c=((c&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;}
        else if(c>=0xC0){if(!p[0])return 0;c=((c&31)<<6)|(p[0]&63);++p;}
        out[n++]=(uint16_t)c;
    }
    out[n]=0;return n;
}
static void pickup(BlackHudPrompts *h,const BlackFlashApi *a,uint64_t signature)
{
    BlackHudPromptField *f=&h->pickup;uint32_t hud=h->hud;
    if(!f->valid)return;
    unsigned state=r8(a,hud+0x2B13);
    if(state>4 || (!state && !r8(a,hud+0x2B10)) || r8(a,hud+0x1C)!=h->requested || r8(a,hud+0x1D)!=h->current ||
        !matches(a,hud+0x27F0,f->rendered,200) || !matches(a,hud+0x2980,h->detail,200)){f->valid=0;return;}
    if(f->signature==signature)return;
    if(black_InputPromptFamily()==BLACK_PROMPTS_XBOX && black_SettingsPadBinding(h->target)==h->target) {
        unsigned len=length(f->raw,200);
        if(a->write(a->opaque,hud+0x27F0,f->raw,(len+1)*2)) {
            memcpy(f->rendered,f->raw,sizeof f->raw);f->signature=signature;
        }
        return;
    }
    char key[32];black_PromptButtonLabel(h->target,0,key,sizeof key);
    uint16_t replacement[32],out[200];unsigned n=units(key,replacement),len=length(f->raw,200);
    if(!n || len-1+n>=200)return;
    memcpy(out,f->raw,h->position*2);memcpy(out+h->position,replacement,n*2);
    memcpy(out+h->position+n,f->raw+h->position+1,(len-h->position)*2);
    if(a->write(a->opaque,hud+0x27F0,out,(len+n)*2)) {
        memset(f->rendered,0,sizeof f->rendered);memcpy(f->rendered,out,(len+n)*2);f->signature=signature;
    }
}
void black_HudPickupReady(BlackHudPrompts *h,const BlackFlashApi *a,uint32_t hud)
{
    if(!current(h,a,hud))return;
    BlackHudPromptField *f=&h->pickup;memset(f,0,sizeof *f);
    uint32_t map=r32(a,0x2F3BD4);if(!map)return;
    uint32_t control=r32(a,map+13*4);int slot=control==19?0:control==16?1:control==17?2:control==18?3:-1;
    if(slot<0 || r32(a,0x2A7DD4+slot*4)!=control)return;
    uint32_t name=r32(a,0x2AB72C+slot*4);uint8_t glyph=r8(a,name);
    static const uint8_t expected[]={0xB0,0xB2,0xB9,0xB3};
    if(!name || glyph!=expected[slot] || r8(a,name+1) || r8(a,hud+0x2B13) || !r8(a,hud+0x2B10) ||
        !copy(a,hud+0x27F0,f->raw,200) || !copy(a,hud+0x2980,h->detail,200))return;
    unsigned count=0,len=length(f->raw,200);
    for(unsigned i=0;i<len;++i)if(f->raw[i]==glyph){h->position=i;++count;}
    if(count!=1)return;
    h->glyph=glyph;h->target=(int)control-16;h->requested=r8(a,hud+0x1C);h->current=r8(a,hud+0x1D);
    memcpy(f->rendered,f->raw,sizeof f->raw);f->capacity=200;f->valid=1;f->signature=0;
    pickup(h,a,black_PromptSignature());
}
void black_HudPromptTick(BlackHudPrompts *h,const BlackFlashApi *a,uint32_t hud)
{
    if(!current(h,a,hud))return;
    uint64_t signature=black_PromptSignature();pickup(h,a,signature);
    uint32_t active=r32(a,hud+0x2110),pending=r32(a,hud+0x22A4);unsigned state=r8(a,hud+0x1B);
    if(active==0xFFFFFFFF || state<5 || state>8)memset(&h->active,0,sizeof h->active);
    if(h->pending.body.valid && active==h->pending.id && pending==0xFFFFFFFF && state>=5 && state<=8 &&
        matches(a,hud+0x1F80,h->pending.body.rendered,200)) {
        h->active=h->pending;memset(&h->pending,0,sizeof h->pending);
    }
    if(h->active.body.valid && active==h->active.id && state>=5 && state<=8) {
        refresh(h,a,&h->active.body,hud+0x1F80,signature);refresh(h,a,&h->active.continuation,hud+0x22C0,signature);
    }
    if(h->pending.body.valid && pending==h->pending.id) {
        refresh(h,a,&h->pending.body,hud+0x2114,signature);refresh(h,a,&h->pending.continuation,hud+0x22C0,signature);
    } else if(pending==0xFFFFFFFF)memset(&h->pending,0,sizeof h->pending);
}
