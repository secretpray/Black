/* Native-C adaptation of TGP game/mouse.cpp and game/name_entry.cpp.
 * Guest operations are explicit callbacks: no X86Context ABI crosses here.
 * Navigate the game's script graph, never force a page transition or save. */
#include "flash_menu.h"
#include "port_menu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

enum { UP=1, DOWN, LEFT, RIGHT, CONFIRM, BACK, MAX_NODES=64 };
static uint32_t r32(BlackFlashMenu *m,uint32_t at)
{
    uint32_t value=0;
    if(++m->reads>32768 || !m->api.read(m->api.opaque,at,&value,4))m->fault=1;
    return value;
}
static int r8(BlackFlashMenu *m,uint32_t at)
{ unsigned char value=0; if(!m->api.read(m->api.opaque,at,&value,1))m->fault=1; return value; }
static float rf(BlackFlashMenu *m,uint32_t at)
{ uint32_t bits=r32(m,at);float value;memcpy(&value,&bits,4);return value; }
static int write32(BlackFlashMenu *m,uint32_t at,uint32_t value)
{ if(m->fault)return 0; if(!m->api.write(m->api.opaque,at,&value,4)){m->fault=1;return 0;}return 1; }
static int string_at(BlackFlashMenu *m,uint32_t at,char *out,size_t cap)
{
    for(size_t i=0;i<cap;++i){out[i]=(char)r8(m,at+(uint32_t)i);if(!out[i])return !m->fault;}
    out[cap-1]=0;m->fault=1;return 0;
}
static uint32_t call(BlackFlashMenu *m,uint32_t target,uint32_t a,uint32_t c,uint32_t d,const uint32_t *args,size_t count)
{ if(m->fault||!target||++m->calls>512){m->fault=1;return 0;}return m->api.call(m->api.opaque,target,a,c,d,args,count); }
static unsigned type(BlackFlashMenu *m,uint32_t value)
{ if(!value)return 0;uint32_t flags=r32(m,value+4);return flags&0x10 ? flags>>25 : 0; }
static int clip(BlackFlashMenu *m,uint32_t value)
{ unsigned t=type(m,value);return t>=0xc&&t<=0x12; }
static uint32_t members(BlackFlashMenu *m,uint32_t value)
{ if(!type(m,value))return 0;return call(m,r32(m,r32(m,value)+0xc),0,value,0,NULL,0); }
static uint32_t find(BlackFlashMenu *m,uint32_t table,const char *name)
{
    if(!table)return 0;uint32_t slots=r32(m,table),items=r32(m,table+4);
    if(slots>4096){m->fault=1;return 0;}
    for(uint32_t i=0;items&&i<slots&&!m->fault;++i){
        uint32_t key=r32(m,items+i*8),value=r32(m,items+i*8+4);char text[96];
        if(key&&key!=0x2ac7ec&&string_at(m,key+8,text,sizeof text)&&!strcasecmp(text,name))return type(m,value)?value:0;
    }
    return 0;
}
static uint32_t member(BlackFlashMenu *m,uint32_t object,const char *name)
{ return find(m,members(m,object),name); }
static float number(BlackFlashMenu *m,uint32_t value)
{ unsigned t=type(m,value);return t==7?(float)(int32_t)r32(m,value+8):t==6?rf(m,value+8):0; }
static uint32_t level(BlackFlashMenu *m)
{
    uint32_t player=r32(m,0x2ec988),movies=player?r32(m,player+0x18):0;
    uint32_t item=movies?r32(m,movies):0;
    for(int i=0;item&&i<256&&!m->fault;++i,item=r32(m,item+0x50)){
        int depth=(int32_t)(r32(m,item+0x54)<<15)>>15;
        if(depth==4)return type(m,item)==0x12?item:0;
    }
    return 0;
}
static int scratch(BlackFlashMenu *m)
{ if(!m->scratch)m->scratch=m->api.alloc(m->api.opaque,16384);return m->scratch!=0; }
static int box(BlackFlashMenu *m,uint32_t object,float bounds[4])
{
    if(!scratch(m))return 0;
    call(m,0xe9310,m->scratch,object,0,NULL,0);
    for(int i=0;i<4;++i)bounds[i]=rf(m,m->scratch+i*4);
    if(m->fault||!isfinite(bounds[0])||!isfinite(bounds[1])||!isfinite(bounds[2])||!isfinite(bounds[3])||
        bounds[2]<bounds[0]||bounds[3]<bounds[1])return 0;
    float x[4]={bounds[0],bounds[2],bounds[0],bounds[2]},y[4]={bounds[1],bounds[1],bounds[3],bounds[3]};
    int shown=!!(r32(m,object+0x58)&0x100000),depth=0;
    for(uint32_t parent=r32(m,object+0x44);parent;parent=r32(m,parent+0x44)){
        if(++depth>32){m->fault=1;return 0;}
        float matrix[6];for(int i=0;i<6;++i){matrix[i]=rf(m,parent+0xc+i*4);if(!isfinite(matrix[i]))return 0;}
        for(int i=0;i<4;++i){float px=x[i];x[i]=matrix[0]*px+matrix[2]*y[i]+matrix[4];y[i]=matrix[1]*px+matrix[3]*y[i]+matrix[5];}
        shown &= !!(r32(m,parent+0x58)&0x100000);
    }
    bounds[0]=fminf(fminf(x[0],x[1]),fminf(x[2],x[3]));bounds[2]=fmaxf(fmaxf(x[0],x[1]),fmaxf(x[2],x[3]));
    bounds[1]=fminf(fminf(y[0],y[1]),fminf(y[2],y[3]));bounds[3]=fmaxf(fmaxf(y[0],y[1]),fmaxf(y[2],y[3]));
    return shown&&!m->fault;
}
static int over(BlackFlashMenu *m,uint32_t object,float x,float y,float pad,float *across)
{
    float b[4];if(!clip(m,object)||!box(m,object,b))return 0;
    if(across)*across=(x-b[0])/fmaxf(b[2]-b[0],1);
    return x>=b[0]-pad&&x<=b[2]+pad&&y>=b[1]-pad&&y<=b[3]+pad;
}
typedef struct {uint32_t object,table,step;} Node;
static const char *button_links[]={"buttonUp","buttonDown","buttonLeft","buttonRight"};
static const char *value_links[]={"upPoint","downPoint","leftPoint","rightPoint"};
static unsigned reach(BlackFlashMenu *m,uint32_t start,const char **links,Node list[MAX_NODES])
{
    if(!start)return 0;unsigned count=1;list[0]=(Node){start,members(m,start),0};
    for(unsigned i=0;i<count&&!m->fault;++i)for(unsigned d=0;d<4;++d){
        uint32_t next=find(m,list[i].table,links[d]);int seen=0;
        for(unsigned j=0;j<count;++j)if(list[j].object==next)seen=1;
        if(next&&!seen&&count<MAX_NODES){list[count]=(Node){next,members(m,next),i?list[i].step:UP+d};++count;}
    }
    return count;
}
static uint32_t value_at(BlackFlashMenu *m,uint32_t values,float x,float y)
{
    Node nodes[MAX_NODES];unsigned count=reach(m,member(m,values,"mActive"),value_links,nodes);
    for(unsigned i=0;i<count;++i)if(over(m,find(m,nodes[i].table,"movieClip"),x,y,6,NULL))return nodes[i].object;
    return 0;
}
static void clear_aim(BlackFlashMenu *m) {m->button=m->values=m->value=m->then=0;}
static void point(BlackFlashMenu *m,Node *nodes,unsigned count,uint32_t map,BlackFlashPointer p)
{
    int clips=0;
    for(unsigned i=0;i<count;++i){
        uint32_t object=nodes[i].object,c=find(m,nodes[i].table,"movieClip"),options=find(m,nodes[i].table,"mChoiceList");
        uint32_t table=members(m,options),value=value_at(m,options,p.x,p.y);clips|=clip(m,c);
        if(value){m->button=object;m->values=options;m->value=p.click?value:0;m->then=0;return;}
        if(over(m,find(m,table,"mLeftAnimation"),p.x,p.y,6,NULL)){m->button=object;m->values=m->value=0;m->then=p.click?LEFT:0;return;}
        if(over(m,find(m,table,"mRightAnimation"),p.x,p.y,6,NULL)){m->button=object;m->values=m->value=0;m->then=p.click?RIGHT:0;return;}
        if(over(m,find(m,table,"mMovieClip"),p.x,p.y,0,NULL)){m->button=object;m->values=m->value=m->then=0;return;}
        if(over(m,c,p.x,p.y,0,NULL)){m->button=object;m->values=m->value=0;m->then=p.click&&!options?CONFIRM:0;return;}
    }
    uint32_t value=p.click?value_at(m,map,p.x,p.y):0;
    if(value){m->button=nodes[0].object;m->values=map;m->value=value;m->then=0;return;}
    clear_aim(m);if(p.click&&!clips){m->button=nodes[0].object;m->then=CONFIRM;}
}
static uint32_t apply(BlackFlashMenu *m,Node *nodes,unsigned count,uint32_t focus)
{
    unsigned i=0;while(i<count&&nodes[i].object!=m->button)++i;
    if(!m->button||i==count){clear_aim(m);return 0;}
    if(m->button!=focus)return nodes[i].step;
    if(m->value){
        Node values[MAX_NODES];unsigned n=reach(m,member(m,m->values,"mActive"),value_links,values);
        for(unsigned j=0;j<n;++j)if(values[j].object==m->value){uint32_t step=values[j].step;if(!step)clear_aim(m);return step;}
        clear_aim(m);return 0;
    }
    uint32_t input=m->then;clear_aim(m);return input;
}
static uint32_t slide(BlackFlashMenu *m,uint32_t row,BlackFlashPointer p)
{
    uint32_t slider=members(m,find(m,row,"mChoiceList")),value=find(m,slider,"mValue");float across=0;
    float step=number(m,find(m,slider,"mChange"));
    if(!value||!(step>0)||!over(m,find(m,slider,"mMovieClip"),p.x,p.y,0,&across))return 0;
    float off=across*100-number(m,value);return off>=step/2?RIGHT:off<=-step/2?LEFT:0;
}
static int page_name(BlackFlashMenu *m,uint32_t ui,char name[64])
{ return ui&&string_at(m,ui+0x3840,name,64); }
void black_FlashMenuTick(BlackFlashMenu *m,uint32_t ui,BlackFlashPointer p)
{
    m->reads=m->calls=0;m->fault=0;char name[64]={0};int menu=page_name(m,ui,name)&&name[0];
    const char *films[]={"FMVPlayer","BootSequence","IntroCredits","BlackIntroSequence"};
    for(int i=0;i<4;++i)if(!strncasecmp(name,films[i],strlen(films[i])))menu=0;
    if(strcmp(name,m->logged_name)){
        snprintf(m->logged_name,sizeof m->logged_name,"%s",name);
        if(m->api.trace)m->api.trace(m->api.opaque,name,ui,0);
    }
    black_GameMenuState(menu,-1);
    if(p.valid&&p.click&&m->api.debug&&m->api.trace){char label[160];
        snprintf(label,sizeof label,"gate pointer=%d menu=%d ready=%d on=%d held=%d leaving=%u start=%u",p.valid,menu,
            r8(m,ui+0x390f),r8(m,ui+0x454),r8(m,ui+0x455),r32(m,ui+0x388c),r32(m,ui+0x450));
        m->api.trace(m->api.opaque,label,ui,0);
    }
    if(!menu||!p.valid||black_MenuIsOpen()){clear_aim(m);m->seen_pointer=0;return;}
    if(r32(m,ui+0x450)){if(p.click&&!m->fault)black_GameMenuConfirm();clear_aim(m);return;}
    if(!r8(m,ui+0x390f)||!r8(m,ui+0x454)||r8(m,ui+0x455)||r32(m,ui+0x388c)){clear_aim(m);return;}
    uint32_t root=level(m),list=member(m,root,"MyMenu"),focus=member(m,list,"mFocusButton");
    if(p.click&&m->api.debug&&m->api.trace){char label[128];snprintf(label,sizeof label,"graph root=%08X menu=%08X focus=%08X fault=%d",root,list,focus,m->fault);m->api.trace(m->api.opaque,label,ui,0);}
    uint32_t display=r32(m,0x2d67f4);float sx=rf(m,ui+0x3710),sy=rf(m,ui+0x3714);
    if(!focus||!display||!(sx>0)||!(sy>0)||!isfinite(sx)||!isfinite(sy)){clear_aim(m);return;}
    p.x=p.x*(float)(int32_t)r32(m,display+0xd428)/sx-(r8(m,display+0xc)?rf(m,0x295da4):0);
    p.y=p.y*(float)(int32_t)r32(m,display+0xd42c)/sy;
    if(r32(m,ui+0x44c)){clear_aim(m);return;} /* keyboard/pad input wins */
    Node nodes[MAX_NODES];unsigned count=reach(m,focus,button_links,nodes);if(!count||m->fault)return;
    if(m->page!=list){clear_aim(m);m->seen_pointer=0;m->page=list;if(m->api.trace)m->api.trace(m->api.opaque,name,list,count);
        if(m->api.debug&&m->api.trace)for(unsigned i=0;i<count&&i<16;++i){
            uint32_t c=find(m,nodes[i].table,"movieClip");float b[4];
            if(c&&clip(m,c)&&box(m,c,b)){
                char label[128];snprintf(label,sizeof label,"bounds %.2f,%.2f..%.2f,%.2f",b[0],b[1],b[2],b[3]);
                m->api.trace(m->api.opaque,label,nodes[i].object,nodes[i].step);
            }
        }
    }
    int moved=!m->seen_pointer||p.x!=m->seen_x||p.y!=m->seen_y;
    m->seen_x=p.x;m->seen_y=p.y;m->seen_pointer=1;
    if(moved||p.click){
        uint32_t previous[]={m->button,m->values,m->value,m->then};
        point(m,nodes,count,member(m,root,"MyMap"),p);
        if(!p.click&&m->button==previous[0]){m->button=previous[0];m->values=previous[1];m->value=previous[2];m->then=previous[3];}
    }
    uint32_t intended=m->button;
    uint32_t input=apply(m,nodes,count,focus);
    if(p.click&&m->api.debug&&m->api.trace){char label[160];
        snprintf(label,sizeof label,"click %.2f,%.2f focus=%08X target=%08X fault=%d",p.x,p.y,focus,intended,m->fault);
        m->api.trace(m->api.opaque,label,ui,input);
    }
    if(!input&&p.down)input=slide(m,nodes[0].table,p);
    if(!input&&p.wheel)input=p.wheel>0?UP:DOWN;
    if(input&&!m->fault)write32(m,ui+0x44c,input);
}
static uint32_t put(BlackFlashMenu *m,int slot,const char *text)
{
    uint32_t at=m->scratch+32+slot*4096;size_t length=strlen(text);
    if(length>=4096||!m->api.write(m->api.opaque,at,text,length+1)){m->fault=1;return 0;}return at;
}
static int get_text(BlackFlashMenu *m,const char *name,char out[256])
{
    char path[96];snprintf(path,sizeof path,"_level4.%s",name);uint32_t result=m->scratch+32+8192;
    unsigned char zero=0;if(!m->api.write(m->api.opaque,result,&zero,1)){m->fault=1;return 0;}
    uint32_t arg=result;call(m,0x101720,put(m,0,path),0,0,&arg,1);
    return !m->fault&&string_at(m,result,out,256);
}
static void page_call(BlackFlashMenu *m,const char *function)
{ uint32_t args[]={put(m,1,function),0,0};call(m,0x101420,put(m,0,"_level4"),0,0,args,3); }
/* The pause menu's own Quit (InGameOn.bin): GoToQuitConfirm sets the
 * confirmation's yes to the game code QuitLevel and opens the game's
 * ConfirmWarning page. 1 once called, -1 on the pause menu before it can take
 * a call, 0 elsewhere. */
int black_FlashPauseQuit(BlackFlashMenu *m,uint32_t ui)
{
    m->reads=m->calls=0;m->fault=0;char page[64]={0};
    if(!page_name(m,ui,page)||strcasecmp(page,"PauseMenu"))return 0;
    if(r32(m,0x2ed408)!=1||!r32(m,0x2ec988)||r32(m,ui+0x381c)!=2||r32(m,ui+0x388c)||r32(m,ui+0x374)||!scratch(m))return -1;
    page_call(m,"GoToQuitConfirm");
    return m->fault?-1:1;
}
int black_FlashOnPage(BlackFlashMenu *m,uint32_t ui,const char *name)
{ char page[64]={0};m->fault=0;return page_name(m,ui,page)&&!strcasecmp(page,name); }
void black_FlashNameTick(BlackFlashMenu *m,uint32_t ui)
{
    m->reads=m->calls=0;m->fault=0;char page[64]={0};
    page_name(m,ui,page);
    if(m->api.debug&&!strcmp(page,"VirtualKeyboard")){
        unsigned gates=r32(m,0x2ed408)|(r32(m,ui+0x381c)<<8)|(!!r32(m,ui+0x388c)<<16)|(r32(m,ui+0x374)<<17);
        if(gates!=m->name_gate&&m->api.trace){m->name_gate=gates;char label[128];
            snprintf(label,sizeof label,"text ready=%u running=%u leaving=%u wait=%u",r32(m,0x2ed408),r32(m,ui+0x381c),r32(m,ui+0x388c),r32(m,ui+0x374));
            m->api.trace(m->api.opaque,label,ui,0);}
    }
    int on=ui&&r32(m,0x2ed408)==1&&r32(m,0x2ec988)&&r32(m,ui+0x381c)==2&&!r32(m,ui+0x388c)&&
        page_name(m,ui,page)&&!strcmp(page,"VirtualKeyboard");
    black_GameMenuState(-1,on);
    if(!on||m->fault||black_MenuIsOpen()||r32(m,ui+0x374)||!scratch(m))return;
    int c=black_GameTextTake();if(c<0)return;
    if(c=='\b'){page_call(m,"BackSpace");return;}if(c==' '){page_call(m,"DoSpace");return;}if(c=='\r'){page_call(m,"DONE");return;}
    if(c>0x7f||!c)return;c=tolower(c);
    if(!isalnum((unsigned char)c)&&!strchr("()&_^%\\/@#[]$;:'\"<>?!-*+={},.",c))return;
    char text[256],limit[256],index[256];
    if(!get_text(m,"stringMaxLength",limit)||!get_text(m,"stringIndex",index)||!get_text(m,"FINALRESULTSTRING",text))return;
    char *end;long max=strtol(limit,&end,10);if(*end||max<1||max>128)return;
    if(!strcmp(text,"undefined"))text[0]=0;size_t len=strlen(text);if(len>=(size_t)max)return;
    long caret=strtol(index,&end,10);if(*end)return;if(caret<0)caret=0;if((size_t)caret>len)caret=(long)len;
    memmove(text+caret+1,text+caret,len-(size_t)caret+1);text[caret]=(char)c;
    uint32_t args[]={put(m,0,"_level4.FINALRESULTSTRING"),put(m,1,text)};call(m,0x101790,0,0,0,args,2);
    page_call(m,"CursorForward");if(write32(m,ui+0x374,0))page_call(m,"UpdateString");
}
