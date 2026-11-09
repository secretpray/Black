/* Explicit page lists: row identifiers stay independent of their position.
 * Shared by the Cocoa panel and navigation checks, outside pinned sources. */
#pragma once
#include "keyboard_mouse.h"
#include "input_profile.h"
#include "game_mods.h"
#include "save_profiles.h"
enum { ROW_SCALE, ROW_UPSCALE, ROW_FOV, ROW_FPS, ROW_BRIGHTNESS, ROW_GAMMA, ROW_AA, ROW_AF, ROW_GAME_LANG, ROW_MENU_LANG, ROW_TEXTURES, ROW_TEXTURE_SIZE,
       ROW_LOOK_X, ROW_LOOK_Y, ROW_AIM_X, ROW_AIM_Y, ROW_INVERT_X, ROW_HOLD_AIM, ROW_AIM_ASSIST,
       ROW_LEFT_DZ, ROW_RIGHT_DZ, ROW_TRIGGER_DZ, ROW_VIBRATION, ROW_RUMBLE,
       ROW_HID, ROW_MEASURE, ROW_RESET, ROW_DEVICE, ROW_PROMPTS,
       ROW_PAD_FIRST, ROW_PAD_LAST = ROW_PAD_FIRST + BLACK_PAD_CONTROLS - 1,
       ROW_MOUSE_X, ROW_MOUSE_Y, ROW_MOUSE_AIM_X, ROW_MOUSE_AIM_Y, ROW_MOUSE_INVERT_X, ROW_MOUSE_INVERT_Y,
       ROW_MOUSE_HOLD, ROW_WALK_SPEED, ROW_BIND_FIRST, ROW_BIND_LAST = ROW_BIND_FIRST + BLACK_ACTION_COUNT - 1,
       ROW_ANY_FILM, ROW_INTROS, ROW_MISSIONS, ROW_MOD_FIRST, ROW_MOD_LAST = ROW_MOD_FIRST + BLACK_MOD_COUNT - 1,
       ROW_NAV_BINDINGS, ROW_NAV_MOUSE, ROW_NAV_PAD, ROW_NAV_ADVANCED, ROW_NAV_DIAGNOSTICS, ROW_NAV_BACKUPS,
       ROW_NAV_PORT, ROW_NAV_CONTROLS, ROW_NAV_MODS, ROW_NAV_SAVES,
       ROW_EDIT_DEVICE, ROW_MOUSE_LOOK_PAIR, ROW_MOUSE_AIM_PAIR, ROW_PAD_LOOK_PAIR, ROW_PAD_AIM_PAIR, ROW_QUIT,
       ROW_SAVE_FIRST, ROW_SAVE_LAST = ROW_SAVE_FIRST + BLACK_PROFILE_SLOTS - 1,
       ROW_SLOT_MISSIONS, ROW_SLOT_IMPORT, ROW_SLOT_EXPORT, ROW_SLOT_DELETE,
       ROW_MISSION_FIRST, ROW_MISSION_LAST = ROW_MISSION_FIRST + BLACK_PROFILE_MISSIONS - 1,
       ROW_BACKUP_FIRST, ROW_BACKUP_LAST = ROW_BACKUP_FIRST + BLACK_SAVE_BACKUPS - 1,
       ROW_PICK_FIRST, ROW_PICK_LAST = ROW_PICK_FIRST + BLACK_SAVE_IMPORTS - 1, ROW_PICK_FOLDER, ROW_PICK_FINDER,
       ROW_BACKUP_RESTORE, ROW_BACKUP_DELETE, ROW_BACKUPS_CLEAR,
       ROW_CONFIRM_YES, ROW_CONFIRM_NO, ROW_CLOSE_MENU, ROW_DEFAULTS, ROW_RESET_ALL, MENU_ROWS };

enum { PAGE_PORT, PAGE_CONTROLS, PAGE_KEYS, PAGE_PAD_BINDINGS, PAGE_MOUSE,
       PAGE_PAD, PAGE_ADVANCED, PAGE_DIAGNOSTICS, PAGE_MODS, PAGE_SAVES, PAGE_BACKUPS, PAGE_SLOT, PAGE_IMPORT_PICK,
       PAGE_MISSIONS, PAGE_CONFIRM, PAGE_BACKUP, PAGE_ROOT, MENU_PAGES };
enum { MENU_TABS = 4, MENU_TAB_MAX_ROWS = 8, TAB_WIDTH = 198 };
/* The Saves pages' rows depend on what is on disk; port_menu.m sets these:
 * backups listed, profiles found to import, the slot page's actions, and the
 * page and row a confirmation returns to. */
static int black_menu_backup_rows = 1, black_menu_pick_rows = 0;
static int black_menu_slot_rows[4] = {ROW_SLOT_MISSIONS}, black_menu_slot_count = 1;
static int black_menu_confirm_page = -1, black_menu_confirm_row = -1;
static inline int black_menu_clamp(int n,int low,int high){return n<low?low:n>high?high:n;}
/* Pages whose last row puts their settings back to the defaults. */
static inline int black_menu_has_defaults(int page)
{return page==PAGE_PORT||page==PAGE_KEYS||page==PAGE_PAD_BINDINGS||page==PAGE_MOUSE||page==PAGE_PAD||page==PAGE_ADVANCED||page==PAGE_MODS;}
static inline int black_menu_count(int page)
{
    static const int counts[]={12,5,1+BLACK_ACTION_COUNT,1+BLACK_PAD_CONTROLS,6,5,8,1,3+BLACK_MOD_COUNT,
        BLACK_PROFILE_SLOTS+1,1,1,2,BLACK_PROFILE_MISSIONS,2,2,6};
    /* The backups, then "delete all" when there are any. */
    if(page==PAGE_BACKUPS)return black_menu_backup_rows>0?black_menu_clamp(black_menu_backup_rows,1,BLACK_SAVE_BACKUPS)+1:1;
    if(page==PAGE_SLOT)return black_menu_clamp(black_menu_slot_count,1,4);
    if(page==PAGE_IMPORT_PICK)return 2+black_menu_clamp(black_menu_pick_rows,0,BLACK_SAVE_IMPORTS);
    /* General also ends with "reset all settings". */
    return page>=0&&page<MENU_PAGES?counts[page]+black_menu_has_defaults(page)+(page==PAGE_PORT):0;
}
static inline int black_menu_row(int page,int index)
{
    static const int controls[]={ROW_DEVICE,ROW_PROMPTS,ROW_NAV_BINDINGS,ROW_NAV_MOUSE,ROW_NAV_PAD};
    static const int mouse[]={ROW_MOUSE_LOOK_PAIR,ROW_MOUSE_AIM_PAIR,ROW_MOUSE_INVERT_X,ROW_MOUSE_INVERT_Y,ROW_MOUSE_HOLD,ROW_WALK_SPEED};
    static const int pad[]={ROW_PAD_LOOK_PAIR,ROW_PAD_AIM_PAIR,ROW_HOLD_AIM,ROW_RUMBLE,ROW_NAV_ADVANCED};
    static const int advanced[]={ROW_INVERT_X,ROW_AIM_ASSIST,ROW_LEFT_DZ,ROW_RIGHT_DZ,ROW_TRIGGER_DZ,ROW_VIBRATION,ROW_HID,ROW_NAV_DIAGNOSTICS};
    if(index<0||index>=black_menu_count(page))return -1;
    if(page==PAGE_PORT&&index==black_menu_count(page)-1)return ROW_RESET_ALL;
    if(black_menu_has_defaults(page)&&index==black_menu_count(page)-1-(page==PAGE_PORT))return ROW_DEFAULTS;
    switch(page){
    case PAGE_PORT:return ROW_SCALE+index;
    case PAGE_ROOT:{static const int root[]={ROW_NAV_PORT,ROW_NAV_CONTROLS,ROW_NAV_MODS,ROW_NAV_SAVES,ROW_CLOSE_MENU,ROW_QUIT};return root[index];}
    case PAGE_CONTROLS:return controls[index];
    case PAGE_KEYS:return index?ROW_BIND_FIRST+index-1:ROW_EDIT_DEVICE;
    case PAGE_PAD_BINDINGS:return index?ROW_PAD_FIRST+index-1:ROW_EDIT_DEVICE;
    case PAGE_MOUSE:return mouse[index];
    case PAGE_PAD:return pad[index];
    case PAGE_ADVANCED:return advanced[index];
    case PAGE_DIAGNOSTICS:return ROW_MEASURE;
    case PAGE_MODS:return ROW_ANY_FILM+index;
    case PAGE_SAVES:return index<BLACK_PROFILE_SLOTS?ROW_SAVE_FIRST+index:ROW_NAV_BACKUPS;
    case PAGE_BACKUPS:return black_menu_backup_rows>0&&index==black_menu_count(page)-1?ROW_BACKUPS_CLEAR:ROW_BACKUP_FIRST+index;
    case PAGE_BACKUP:return index?ROW_BACKUP_DELETE:ROW_BACKUP_RESTORE;
    case PAGE_SLOT:return black_menu_slot_rows[index];
    case PAGE_MISSIONS:return ROW_MISSION_FIRST+index;
    case PAGE_CONFIRM:return index?ROW_CONFIRM_NO:ROW_CONFIRM_YES;
    case PAGE_IMPORT_PICK:{int picks=black_menu_count(page)-2;return index<picks?ROW_PICK_FIRST+index:index==picks?ROW_PICK_FOLDER:ROW_PICK_FINDER;}
    default:return -1;
    }
}
static inline int black_menu_index(int page,int row)
{for(int i=0;i<black_menu_count(page);++i)if(black_menu_row(page,i)==row)return i;return -1;}
/* The section a page belongs to (0 port, 1 controls, 2 mods, 3 saves), -1
 * for the top menu that lists them. */
static inline int black_menu_tab(int page){return page==PAGE_ROOT?-1:page==PAGE_PORT?0:page==PAGE_MODS?2:page>=PAGE_SAVES?3:1;}
static inline int black_menu_top(int tab){return tab==0?PAGE_PORT:tab==2?PAGE_MODS:tab==3?PAGE_SAVES:PAGE_CONTROLS;}
/* The page Back leaves a page for, and the row it lands on there; -1 at the top. */
static inline int black_menu_parent(int page,int *row)
{
    int r=-1,p=-1;
    switch(page){
    case PAGE_PORT:p=PAGE_ROOT;r=ROW_NAV_PORT;break;
    case PAGE_CONTROLS:p=PAGE_ROOT;r=ROW_NAV_CONTROLS;break;
    case PAGE_MODS:p=PAGE_ROOT;r=ROW_NAV_MODS;break;
    case PAGE_SAVES:p=PAGE_ROOT;r=ROW_NAV_SAVES;break;
    case PAGE_KEYS:case PAGE_PAD_BINDINGS:p=PAGE_CONTROLS;r=ROW_NAV_BINDINGS;break;
    case PAGE_MOUSE:p=PAGE_CONTROLS;r=ROW_NAV_MOUSE;break;
    case PAGE_PAD:p=PAGE_CONTROLS;r=ROW_NAV_PAD;break;
    case PAGE_ADVANCED:p=PAGE_PAD;r=ROW_NAV_ADVANCED;break;
    case PAGE_DIAGNOSTICS:p=PAGE_ADVANCED;r=ROW_NAV_DIAGNOSTICS;break;
    case PAGE_BACKUPS:p=PAGE_SAVES;r=ROW_NAV_BACKUPS;break;
    case PAGE_SLOT:p=PAGE_SAVES;r=-1;break; /* port_menu.m returns to the slot's row */
    case PAGE_IMPORT_PICK:p=PAGE_SLOT;r=ROW_SLOT_IMPORT;break;
    case PAGE_MISSIONS:p=PAGE_SLOT;r=ROW_SLOT_MISSIONS;break;
    case PAGE_BACKUP:p=PAGE_BACKUPS;r=-1;break; /* port_menu.m returns to the backup's row */
    case PAGE_CONFIRM:p=black_menu_confirm_page;r=black_menu_confirm_row;break;
    }
    if(row)*row=r;
    return p;
}
static inline int black_menu_pair(int row)
{return row>=ROW_MOUSE_LOOK_PAIR&&row<=ROW_PAD_AIM_PAIR;}
static inline int black_menu_link(int row)
{return row>=ROW_NAV_BINDINGS&&row<=ROW_NAV_SAVES;}
/* Rows of the Saves section other than its link to the backups. */
static inline int black_menu_save(int row)
{return row>=ROW_SAVE_FIRST&&row<=ROW_CONFIRM_NO;}
