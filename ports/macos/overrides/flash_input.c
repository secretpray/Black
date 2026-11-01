/* Thread boundary: guest Flash state, local Cocoa pointer/text events. */
#include "flash_menu.h"
#include "keyboard_mouse.h"
#include "port_menu.h"
#include <math.h>
#include <pthread.h>
#include <string.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int menu_on,text_on,pointer_inside,pointer_down,pointer_click,wheel;
static float px,py;
static uint64_t seen_ms,intent_until,confirm_until;
static unsigned chars[64],head,count;
void black_GameMenuState(int menu,int text)
{
    int changed=0;
    pthread_mutex_lock(&lock);
    if(menu>=0&&menu_on!=!!menu){menu_on=!!menu;changed=1;pointer_down=pointer_click=wheel=0;}
    if(text>=0&&text_on!=!!text){text_on=!!text;head=count=0;changed=1;}
    seen_ms=black_InputNowMs();
    pthread_mutex_unlock(&lock);
    if(changed)black_KeyReleaseAll();
}
int black_GameMenuActive(void)
{ pthread_mutex_lock(&lock);int on=menu_on&&black_InputNowMs()-seen_ms<500;pthread_mutex_unlock(&lock);return on; }
int black_GameTextActive(void)
{ pthread_mutex_lock(&lock);int on=text_on&&black_InputNowMs()-seen_ms<500;pthread_mutex_unlock(&lock);return on; }
void black_GamePointerPosition(float x,float y,int inside)
{
    pthread_mutex_lock(&lock);px=x;py=y;pointer_inside=inside&&isfinite(x)&&isfinite(y)&&x>=0&&x<=1&&y>=0&&y<=1;
    pthread_mutex_unlock(&lock);
}
void black_GamePointerButton(int down)
{
    pthread_mutex_lock(&lock);
    if(down&&!pointer_down){pointer_click=1;intent_until=black_InputNowMs()+100;}
    pointer_down=!!down;pthread_mutex_unlock(&lock);
}
void black_GamePointerWheel(int up)
{ pthread_mutex_lock(&lock);wheel=up?1:-1;intent_until=black_InputNowMs()+100;pthread_mutex_unlock(&lock); }
BlackFlashPointer black_GamePointerTake(void)
{
    pthread_mutex_lock(&lock);
    BlackFlashPointer p={px,py,pointer_inside&&menu_on,pointer_down,pointer_click,wheel};
    if(!black_InputKeyboardActive()||black_MenuIsOpen())p.valid=0;
    if(p.valid){pointer_click=wheel=0;}
    pthread_mutex_unlock(&lock);return p;
}
int black_GamePointerIntent(void)
{ pthread_mutex_lock(&lock);int active=black_InputNowMs()<intent_until;pthread_mutex_unlock(&lock);return active; }
void black_GameMenuConfirm(void)
{ pthread_mutex_lock(&lock);confirm_until=black_InputNowMs()+80;pthread_mutex_unlock(&lock); }
/* Used by native keyboard mapping, not by the guest menu hook. */
int black_GameConfirmPending(void)
{ pthread_mutex_lock(&lock);int active=black_InputNowMs()<confirm_until;pthread_mutex_unlock(&lock);return active; }
void black_GameTextCharacter(unsigned code)
{
    if(!code||code>127)return;
    pthread_mutex_lock(&lock);
    if(text_on&&count<64){chars[(head+count)%64]=code;++count;intent_until=black_InputNowMs()+100;}
    pthread_mutex_unlock(&lock);
}
int black_GameTextTake(void)
{
    pthread_mutex_lock(&lock);int c=-1;if(count){c=(int)chars[head];head=(head+1)%64;--count;}pthread_mutex_unlock(&lock);return c;
}
void black_GamePointerRelease(void)
{
    pthread_mutex_lock(&lock);
    pointer_inside=pointer_down=pointer_click=wheel=0;intent_until=confirm_until=0;head=count=0;
    pthread_mutex_unlock(&lock);
}
int black_FramePointer(float x,float y,float w,float h,float aspect,float *nx,float *ny)
{
    if(!isfinite(x)||!isfinite(y)||!(w>0)||!(h>0)||!(aspect>0)||!isfinite(aspect))return 0;
    float vw=w,vh=w/aspect;if(vh>h){vh=h;vw=h*aspect;}
    *nx=(x-(w-vw)*.5f)/vw;*ny=1-(y-(h-vh)*.5f)/vh;
    return *nx>=0&&*nx<=1&&*ny>=0&&*ny<=1;
}
