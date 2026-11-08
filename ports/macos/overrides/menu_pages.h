/* Explicit page lists: row identifiers stay independent of their position.
 * Shared by the Cocoa panel and navigation checks, outside pinned sources. */
#pragma once
#include "keyboard_mouse.h"
#include "input_profile.h"
#include "game_mods.h"
enum { ROW_SCALE, ROW_UPSCALE, ROW_FOV, ROW_FPS, ROW_AA, ROW_AF, ROW_GAME_LANG, ROW_MENU_LANG, ROW_TEXTURES, ROW_TEXTURE_SIZE,
       ROW_LOOK_X, ROW_LOOK_Y, ROW_AIM_X, ROW_AIM_Y, ROW_INVERT_X, ROW_HOLD_AIM, ROW_AIM_ASSIST,
       ROW_LEFT_DZ, ROW_RIGHT_DZ, ROW_TRIGGER_DZ, ROW_VIBRATION, ROW_RUMBLE,
       ROW_HID, ROW_MEASURE, ROW_RESET, ROW_DEVICE, ROW_PROMPTS,
       ROW_PAD_FIRST, ROW_PAD_LAST = ROW_PAD_FIRST + BLACK_PAD_CONTROLS - 1,
       ROW_MOUSE_X, ROW_MOUSE_Y, ROW_MOUSE_AIM_X, ROW_MOUSE_AIM_Y, ROW_MOUSE_INVERT_X, ROW_MOUSE_INVERT_Y,
       ROW_MOUSE_HOLD, ROW_WALK_SPEED, ROW_BIND_FIRST, ROW_BIND_LAST = ROW_BIND_FIRST + BLACK_ACTION_COUNT - 1,
       ROW_ANY_FILM, ROW_INTROS, ROW_MISSIONS, ROW_MOD_FIRST, ROW_MOD_LAST = ROW_MOD_FIRST + BLACK_MOD_COUNT - 1,
       ROW_NAV_BINDINGS, ROW_NAV_MOUSE, ROW_NAV_PAD, ROW_NAV_ADVANCED, ROW_NAV_DIAGNOSTICS,
       ROW_EDIT_DEVICE, ROW_MOUSE_LOOK_PAIR, ROW_MOUSE_AIM_PAIR, ROW_PAD_LOOK_PAIR, ROW_PAD_AIM_PAIR, ROW_QUIT, MENU_ROWS };

enum { PAGE_PORT, PAGE_CONTROLS, PAGE_KEYS, PAGE_PAD_BINDINGS, PAGE_MOUSE,
       PAGE_PAD, PAGE_ADVANCED, PAGE_DIAGNOSTICS, PAGE_MODS, MENU_PAGES };
enum { MENU_TABS = 3, MENU_TAB_MAX_ROWS = 5, TAB_WIDTH = 264 };
static inline int black_menu_count(int page)
{
    static const int counts[]={11,5,1+BLACK_ACTION_COUNT,1+BLACK_PAD_CONTROLS,6,5,8,1,3+BLACK_MOD_COUNT};
    return page>=0&&page<MENU_PAGES?counts[page]:0;
}
static inline int black_menu_row(int page,int index)
{
    static const int controls[]={ROW_DEVICE,ROW_PROMPTS,ROW_NAV_BINDINGS,ROW_NAV_MOUSE,ROW_NAV_PAD};
    static const int mouse[]={ROW_MOUSE_LOOK_PAIR,ROW_MOUSE_AIM_PAIR,ROW_MOUSE_INVERT_X,ROW_MOUSE_INVERT_Y,ROW_MOUSE_HOLD,ROW_WALK_SPEED};
    static const int pad[]={ROW_PAD_LOOK_PAIR,ROW_PAD_AIM_PAIR,ROW_HOLD_AIM,ROW_RUMBLE,ROW_NAV_ADVANCED};
    static const int advanced[]={ROW_INVERT_X,ROW_AIM_ASSIST,ROW_LEFT_DZ,ROW_RIGHT_DZ,ROW_TRIGGER_DZ,ROW_VIBRATION,ROW_HID,ROW_NAV_DIAGNOSTICS};
    if(index<0||index>=black_menu_count(page))return -1;
    switch(page){
    case PAGE_PORT:return index<10?ROW_SCALE+index:ROW_QUIT;
    case PAGE_CONTROLS:return controls[index];
    case PAGE_KEYS:return index?ROW_BIND_FIRST+index-1:ROW_EDIT_DEVICE;
    case PAGE_PAD_BINDINGS:return index?ROW_PAD_FIRST+index-1:ROW_EDIT_DEVICE;
    case PAGE_MOUSE:return mouse[index];
    case PAGE_PAD:return pad[index];
    case PAGE_ADVANCED:return advanced[index];
    case PAGE_DIAGNOSTICS:return ROW_MEASURE;
    case PAGE_MODS:return ROW_ANY_FILM+index;
    default:return -1;
    }
}
static inline int black_menu_index(int page,int row)
{for(int i=0;i<black_menu_count(page);++i)if(black_menu_row(page,i)==row)return i;return -1;}
static inline int black_menu_tab(int page){return page==PAGE_PORT?0:page==PAGE_MODS?2:1;}
static inline int black_menu_top(int tab){return tab==0?PAGE_PORT:tab==2?PAGE_MODS:PAGE_CONTROLS;}
static inline int black_menu_pair(int row)
{return row>=ROW_MOUSE_LOOK_PAIR&&row<=ROW_PAD_AIM_PAIR;}
static inline int black_menu_link(int row)
{return row>=ROW_NAV_BINDINGS&&row<=ROW_NAV_DIAGNOSTICS;}
