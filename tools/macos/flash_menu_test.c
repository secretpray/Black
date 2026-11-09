/* The production graph/name logic against a checked in-memory guest fixture.
 * Guest calls remain observable; no game assets or actual profile writes. */
#include "../../ports/macos/overrides/flash_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/port_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static unsigned char ram[0x600000];
static uint32_t cursor=0x10000,ui,display,root,list,b1,b2,c1,c2;
static unsigned calls,confirm_calls,updates;
static char name[256]="abcd";static int caret=2,max_length=12;
static int checkpoint_offer,checkpoint_prepared,checkpoint_asked,checkpoint_active=1;
static unsigned resume_calls,run_actions,label_sets,duplicates;
static char label_text[128];
static uint32_t last_string,stock_text_field,item_text_field;
static uint32_t text_anim,lead_clip,stock_clip;
static int rd(void *p,uint32_t at,void *out,size_t n)
{(void)p;if(at<0x10000||(uint64_t)at+n>sizeof ram)return 0;memcpy(out,ram+at,n);return 1;}
static int wr(void *p,uint32_t at,const void *in,size_t n)
{(void)p;if(at<0x10000||(uint64_t)at+n>sizeof ram)return 0;memcpy(ram+at,in,n);return 1;}
static uint32_t r(uint32_t at){uint32_t v=0;if(!rd(NULL,at,&v,4))abort();return v;}
static void w(uint32_t at,uint32_t v){if(!wr(NULL,at,&v,4))abort();}
static void f(uint32_t at,float v){if(!wr(NULL,at,&v,4))abort();}
static float rf(uint32_t at){float v=0;if(!rd(NULL,at,&v,4))abort();return v;}
static uint32_t allocate(void *p,size_t n){(void)p;uint32_t at=cursor;cursor+=(uint32_t)((n+15)&~15);if(cursor>0x200000)abort();return at;}
static uint32_t object(unsigned type)
{uint32_t at=allocate(NULL,0x100);w(at,0x500000);w(at+4,(type<<25)|0x10);w(0x50000c,0x200);return at;}
static void field(uint32_t obj,const char *key,uint32_t value)
{
    uint32_t table=r(obj+0x60);if(!table){table=allocate(NULL,2048);w(obj+0x60,table);w(table+4,table+16);}
    unsigned count=r(table);uint32_t entries=r(table+4);
    for(unsigned i=0;i<count;++i)if(!strcmp((char*)ram+r(entries+i*8)+8,key)){w(entries+i*8+4,value);return;}
    uint32_t text=allocate(NULL,strlen(key)+9);strcpy((char*)ram+text+8,key);
    w(entries+count*8,text);w(entries+count*8+4,value);w(table,count+1);
}
static uint32_t make_clip(float x,float y,float width,float height)
{uint32_t c=object(0xd);w(c+0x58,0x100000);f(c+0x70,x);f(c+0x74,y);f(c+0x78,x+width);f(c+0x7c,y+height);return c;}
static uint32_t get(uint32_t obj,const char *key)
{
    uint32_t table=r(obj+0x60);if(!table)return 0;
    unsigned count=r(table);uint32_t entries=r(table+4);
    for(unsigned i=0;i<count;++i)if(!strcmp((char*)ram+r(entries+i*8)+8,key))return r(entries+i*8+4);
    return 0;
}
static int checkpoint_stub(void *p,int mode){(void)p;if(mode==2)return checkpoint_active;++checkpoint_asked;if(mode)++checkpoint_prepared;return checkpoint_offer;}
static const char *label_of(uint32_t text_field)
{uint32_t string=get(text_field,"variable");return string?(char*)ram+string+8:"";}
static uint32_t guest_call(void *p,uint32_t target,uint32_t a,uint32_t ec,uint32_t ed,const uint32_t *args,size_t n)
{
    (void)p;(void)ed;++calls;
    if(target==0x200)return r(ec+0x60);
    if(target==0xe9310){if(n)abort();memcpy(ram+a,ram+ec+0x70,16);return 0;}
    if(target==0x101720){if(n!=1)abort();const char *path=(char*)ram+a;char out[256];
        if(strstr(path,"stringMaxLength"))snprintf(out,sizeof out,"%d",max_length);
        else if(strstr(path,"stringIndex"))snprintf(out,sizeof out,"%d",caret);else snprintf(out,sizeof out,"%s",name);
        strcpy((char*)ram+args[0],out);return 0;}
    if(target==0x101790){if(n!=2||strcmp((char*)ram+args[0],"_level4.FINALRESULTSTRING"))abort();strcpy(name,(char*)ram+args[1]);return 0;}
    if(target==0x101420&&n==5){ /* title item: a stopped copy of the last label */
        const char *fn=(char*)ram+args[0];uint32_t data,list,sentinel,vfield,fdata,nametext;
        if(strcmp(fn,"duplicateMovieClip")||args[2]!=2||strcmp((char*)ram+args[3],"CheckpointButton")||
            strcmp((char*)ram+args[4],"100")||!strstr((char*)ram+a,".TextAnim."))abort();
        data=allocate(NULL,0x40);list=allocate(NULL,16);sentinel=allocate(NULL,0x60);
        vfield=object(0xF);fdata=allocate(NULL,0x40);nametext=allocate(NULL,32);
        strcpy((char*)ram+nametext+8,"variable");w(fdata+0x1C,nametext);w(vfield+0x48,fdata);
        w(sentinel+0x50,vfield);w(list,sentinel);w(data+0x24,list);
        {uint32_t clip=object(0xD);w(clip+0x48,data);w(clip+0x58,0x100000);field(text_anim,"CheckpointButton",clip);}
        item_text_field=vfield;++duplicates;return 0;
    }
    if(target==0x100DB0){ /* SetMember: eax value, edx kept name, object in the args */
        const char *key=(char*)ram+ed+12;
        if(!strcmp(key,"variable")){++label_sets;if(a!=last_string)abort();field(args[1],key,a);return 1;}
        if(ed+12<0x10000)abort();
        field(args[1],key,a);return 1;
    }
    if(target==0xF5600){if(n!=1)abort();snprintf(label_text,sizeof label_text,"%s",(char*)ram+args[0]);
        last_string=allocate(NULL,160);snprintf((char*)ram+last_string+8,152,"%s",label_text);return last_string;}
    if(target==0x10F180){if(n!=1||args[0]!=r(0x2ec988))abort();++run_actions;return 0;}
    if(target==0x124CF0){++resume_calls;strcpy((char*)ram+ui+0x3820,"FMVPlayer");w(ui+0x3884,1);return 0;}
    if(target==0x101420){if(n!=3||strcmp((char*)ram+a,"_level4"))abort();const char *fn=(char*)ram+args[0];
        if(!strcmp(fn,"CursorForward")){++caret;w(ui+0x374,1);}
        else if(!strcmp(fn,"UpdateString")){if(r(ui+0x374))abort();++updates;w(ui+0x374,1);}
        else if(!strcmp(fn,"DONE"))++confirm_calls;
        else if(!strcmp(fn,"BackSpace")){if(caret>0){memmove(name+caret-1,name+caret,strlen(name)-caret+1);--caret;}}
        else if(!strcmp(fn,"DoSpace")){if((int)strlen(name)<max_length){memmove(name+caret+1,name+caret,strlen(name)-caret+1);name[caret++]=' ';}}
        else abort();return 0;}
    abort();
}
static BlackFlashMenu setup(void)
{
    memset(ram,0,sizeof ram);cursor=0x10000;black_GameMenuState(0,0);black_InputSetKeyboardActive(1);
    ui=allocate(NULL,0x4000);strcpy((char*)ram+ui+0x3840,"OptionsMenuXbox");ram[ui+0x390f]=ram[ui+0x454]=1;
    f(ui+0x3710,1);f(ui+0x3714,1);w(ui+0x381c,2);
    display=allocate(NULL,0xd500);w(0x2d67f4,display);w(display+0xd428,640);w(display+0xd42c,480);
    uint32_t player=allocate(NULL,0x100),movies=allocate(NULL,16);w(0x2ec988,player);w(0x2ed408,1);w(player+0x18,movies);
    root=object(0x12);w(root+0x54,4);w(movies,root);list=object(8);field(root,"MyMenu",list);
    b1=object(8);b2=object(8);c1=make_clip(100,100,150,40);c2=make_clip(100,180,150,40);
    field(b1,"movieClip",c1);field(b2,"movieClip",c2);field(b1,"buttonDown",b2);field(b2,"buttonUp",b1);field(list,"mFocusButton",b1);
    return (BlackFlashMenu){.api={NULL,rd,wr,guest_call,allocate,NULL,0,checkpoint_stub}};
}
static BlackFlashPointer pointer(float x,float y,int click)
{return (BlackFlashPointer){x/640,y/480,1,click,click,0};}
static unsigned kind(uint32_t v){return r(v+4)>>25;}
/* The title menu: Main.TextAnim holds the last stock label, the ring's first
 * and last buttons sit on the movie, and the button class has the highlight
 * methods. */
static void setup_title(void)
{
    uint32_t main_object=object(0xC),first=object(8),last=object(8),klass=object(8);
    black_MenuSetOpen(0);checkpoint_offer=1;checkpoint_prepared=checkpoint_asked=0;
    resume_calls=run_actions=label_sets=duplicates=0;label_text[0]=0;item_text_field=0;
    strcpy((char*)ram+ui+0x3840,"blacktitlemenu");
    field(root,"Main",main_object);
    text_anim=object(0xC);field(main_object,"TextAnim",text_anim);
    lead_clip=object(0xD);field(text_anim,"operatorprofile_text",lead_clip);
    field(root,"continueButton",first);field(root,"profileButton",last);
    /* The stock Continue's label is its own TextAnim clip, $FE_CONTINUEMISSION. */
    {
        uint32_t clip=object(0xD),data=allocate(NULL,0x40),list=allocate(NULL,16),sentinel=allocate(NULL,0x60),nametext=allocate(NULL,32);
        stock_text_field=object(0xF);
        strcpy((char*)ram+nametext+8,"variable");
        w(stock_text_field+0x48,allocate(NULL,0x40));w(r(stock_text_field+0x48)+0x1C,nametext);
        w(sentinel+0x50,stock_text_field);w(list,sentinel);w(data+0x24,list);w(clip+0x48,data);
        f(clip+0x1C,100);f(clip+0x20,150);stock_clip=clip;
        field(text_anim,"continuemission_text",clip);
    }
    field(last,"buttonDown",first);field(first,"buttonUp",last);
    field(first,"HighlightFrame",object(8));field(klass,"HighLight",object(8));field(klass,"UnHighLight",object(8));
    w(r(first+0x60)+8,klass);
    field(list,"mFocusButton",last);
}
int main(void)
{
    BlackFlashMenu m=setup();BlackFlashPointer p=pointer(150,200,0);
    black_FlashMenuTick(&m,ui,p);if(m.fault||r(ui+0x44c)!=2)return 1; /* hover navigates down */
    field(list,"mFocusButton",b2);w(ui+0x44c,0);p.click=p.down=1;
    black_FlashMenuTick(&m,ui,p);if(m.fault||r(ui+0x44c)!=5)return 2;
    w(ui+0x44c,0);p.click=0;black_FlashMenuTick(&m,ui,p);if(r(ui+0x44c))return 3; /* hold does not repeat confirm */
    w(ui+0x44c,1);p.click=1;black_FlashMenuTick(&m,ui,p);if(r(ui+0x44c)!=1)return 4; /* pad/key input wins */
    w(ui+0x44c,0);w(c2+0x58,0);black_FlashMenuTick(&m,ui,p);if(r(ui+0x44c))return 5;
    w(c2+0x58,0x100000);p=pointer(500,400,0);p.wheel=-1;black_FlashMenuTick(&m,ui,p);if(r(ui+0x44c)!=2)return 6;
    w(ui+0x44c,0);w(c2+0x44,c2);p=pointer(150,200,1);black_FlashMenuTick(&m,ui,p);
    if(!m.fault||r(ui+0x44c))return 7; /* parent cycle is rejected */
    m=setup();w(r(b1+0x60),4097);black_FlashMenuTick(&m,ui,p);if(!m.fault||r(ui+0x44c))return 8;
    m=setup();strcpy((char*)ram+ui+0x3840,"FMVPlayer");black_FlashMenuTick(&m,ui,p);if(black_GameMenuActive()||r(ui+0x44c))return 9;
    m=setup();ram[display+0xc]=1;w(display+0xd428,1280);w(display+0xd42c,720);f(ui+0x3710,1.5);f(ui+0x3714,1.5);f(0x295da4,320.0f/3);
    p=(BlackFlashPointer){(150+320.0f/3)*1.5f/1280,200.0f*1.5f/720,1,0,0,0};
    black_FlashMenuTick(&m,ui,p);if(m.fault||r(ui+0x44c)!=2)return 10;
    m=setup();uint32_t slider=object(8),val=object(7),step=object(7),bar=make_clip(100,180,200,30);
    w(val+8,25);w(step+8,10);field(slider,"mValue",val);field(slider,"mChange",step);field(slider,"mMovieClip",bar);
    field(b1,"mChoiceList",slider);p=pointer(270,190,1);black_FlashMenuTick(&m,ui,p);if(m.fault||r(ui+0x44c)!=4)return 11;
    m=setup();strcpy((char*)ram+ui+0x3840,"VirtualKeyboard");black_FlashNameTick(&m,ui);
    black_GameTextCharacter('Z');black_FlashNameTick(&m,ui);if(strcmp(name,"abzcd")||caret!=3||updates!=1)return 12;
    black_GameTextCharacter('x');black_GameTextCharacter('y');black_FlashNameTick(&m,ui);if(strcmp(name,"abzcd"))return 13;
    w(ui+0x374,0);black_FlashNameTick(&m,ui);if(strcmp(name,"abzxcd"))return 14;
    w(ui+0x374,0);black_FlashNameTick(&m,ui);if(strcmp(name,"abzxycd"))return 15;
    w(ui+0x374,0);black_GameTextCharacter('\b');black_FlashNameTick(&m,ui);if(strcmp(name,"abzxcd"))return 16;
    black_GameTextCharacter(' ');black_FlashNameTick(&m,ui);if(strcmp(name,"abzx cd"))return 17;
    max_length=(int)strlen(name);black_GameTextCharacter('q');black_FlashNameTick(&m,ui);if(strcmp(name,"abzx cd"))return 18;
    black_GameTextCharacter('\r');strcpy((char*)ram+ui+0x3840,"OptionsMenuXbox");black_FlashNameTick(&m,ui);
    if(black_GameTextActive()||black_GameTextTake()!=-1||confirm_calls)return 19; /* queued Enter cannot confirm the next page */
    /* The checkpoint item: one copy of the last label, under it, linked into
     * the ring, and resuming through the title's own start action. */
    m=setup();setup_title();black_FlashCheckpointItem(&m,ui);
    {
        uint32_t button=get(text_anim,"CheckpointButton");
        uint32_t first=get(root,"continueButton"),last=get(root,"profileButton");
        if(m.fault||!button||kind(button)!=0xD)return 25;
        if(label_sets!=2||strcmp(label_text,"$FE_STARTMISSION")||
            strcmp(label_of(item_text_field),"$FE_CONTINUEMISSION")||
            strcmp(label_of(stock_text_field),"$FE_STARTMISSION")||
            run_actions!=1||duplicates!=1||checkpoint_asked!=1)return 26;
        if(get(last,"buttonDown")!=button||get(button,"buttonUp")!=last||
            get(button,"buttonDown")!=first||get(first,"buttonUp")!=button)return 27;
        {
            float y=125.0f;uint32_t bits;
            memcpy(&bits,&y,4);
            /* Above the stock Continue row, which the item is ringed to. */
            if(r(button+0x1C)!=r(stock_clip+0x1C)||r(button+0x20)!=bits||
                !(rf(button+0x20)<rf(stock_clip+0x20)))return 28;
        }
        black_FlashCheckpointItem(&m,ui);if(duplicates!=1)return 29;
        field(list,"mFocusButton",first);w(ui+0x44c,5);
        if(black_FlashCheckpointConfirm(&m,ui)||checkpoint_prepared)return 30; /* not on another button */
        field(list,"mFocusButton",button);w(ui+0x44c,5);
        if(black_FlashCheckpointConfirm(&m,ui)!=1||checkpoint_prepared!=1||resume_calls!=1)return 31;
        black_FlashCheckpointItem(&m,ui);if(duplicates!=1||r(ui+0x44c)!=5)return 32; /* the item leaves the queue alone */
    }
    /* The file gone by the press: the press is taken, nothing starts, the
     * item goes on the next tick. */
    m=setup();setup_title();black_FlashCheckpointItem(&m,ui);
    {
        uint32_t button=get(text_anim,"CheckpointButton");int calls=resume_calls;
        if(!button)return 41;
        field(list,"mFocusButton",button);w(ui+0x44c,5);checkpoint_offer=0;
        if(black_FlashCheckpointConfirm(&m,ui)!=1||resume_calls!=calls)return 42;
        black_FlashCheckpointItem(&m,ui);if(get(button,"buttonDown")==get(root,"continueButton"))return 43;
        checkpoint_offer=1;
    }
    /* Without a resume point, or once it goes away, no item and no focus on it. */
    m=setup();setup_title();checkpoint_offer=0;black_FlashCheckpointItem(&m,ui);
    if(get(text_anim,"CheckpointButton")||duplicates||label_sets!=1||strcmp(label_of(stock_text_field),"$FE_STARTMISSION"))return 33;
    /* Checkpoints off: the stock button keeps its own label. */
    m=setup();setup_title();checkpoint_offer=0;checkpoint_active=0;black_FlashCheckpointItem(&m,ui);
    if(get(text_anim,"CheckpointButton")||label_sets||get(stock_text_field,"variable"))return 44;
    checkpoint_active=1;
    m=setup();setup_title();black_FlashCheckpointItem(&m,ui);
    {
        uint32_t button=get(text_anim,"CheckpointButton"),first=get(root,"continueButton"),last=get(root,"profileButton");
        if(!button)return 34;
        checkpoint_offer=0;field(list,"mFocusButton",button);
        black_MenuSetOpen(1);black_FlashCheckpointItem(&m,ui);
        if(get(button,"buttonDown")!=first)return 35; /* open panel: the item is left alone */
        black_MenuSetOpen(0);black_FlashCheckpointItem(&m,ui);
        if(get(last,"buttonDown")!=first||get(first,"buttonUp")!=last)return 36;
        if(get(button,"buttonDown")||get(button,"buttonUp"))return 37;
        if(r(button+0x58)&0x100000)return 38;
        if(get(list,"mFocusButton")!=last)return 39;
        /* The checkpoint gone, still Start Mission; checkpoints off, its own label. */
        if(label_sets!=2||strcmp(label_of(stock_text_field),"$FE_STARTMISSION"))return 40;
        checkpoint_active=0;m.cp_checked=0;black_FlashCheckpointItem(&m,ui);
        if(label_sets!=3||strcmp(label_of(stock_text_field),"$FE_CONTINUEMISSION"))return 45;
        checkpoint_active=1;
    }
    puts("Production Flash graph: hover/click/input priority/hidden/cycles/wheel/widescreen/slider; native script text/caret/limits/one-per-tick/page reset; quick Continue once at the title; checkpoint item made, linked, confirming the title's start, unlinked and hidden without one: PASS");return 0;
}
