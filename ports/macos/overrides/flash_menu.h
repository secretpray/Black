#ifndef BLACK_FLASH_MENU_H
#define BLACK_FLASH_MENU_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    float x, y; /* normalized displayed frame, top-left origin */
    int valid, down, click, wheel;
} BlackFlashPointer;
typedef struct {
    void *opaque;
    int (*read)(void *, uint32_t, void *, size_t);
    int (*write)(void *, uint32_t, const void *, size_t);
    uint32_t (*call)(void *, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t *, size_t);
    uint32_t (*alloc)(void *, size_t);
    void (*trace)(void *, const char *, uint32_t, unsigned);
    int debug;
    /* The title menu's checkpoint item: 1 when the save this profile plays has
     * a checkpoint to resume; prepare asks the game to resume it. */
    int (*checkpoint)(void *, int mode); /* 0: a resume point to offer, 1: prepare it, 2: checkpoints on */
} BlackFlashApi;
typedef struct {
    BlackFlashApi api;
    uint32_t button, values, value, then, page, scratch;
    float seen_x, seen_y;
    unsigned reads, calls, name_gate;
    int fault, seen_pointer;
    char logged_name[64];
    uint32_t cp_list, cp_button, cp_names, cp_stock;
    int cp_offer, cp_active, cp_checked, cp_linked, cp_panel, cp_reported, cp_stock_named;
    uint32_t cp_row_list, cp_row, cp_row_on, cp_row_off;
    int cp_row_linked, cp_row_failed, cp_row_value;
    float cp_row_dy;
} BlackFlashMenu;
void black_FlashMenuTick(BlackFlashMenu *, uint32_t ui, BlackFlashPointer);
/* The title menu's resume item, on every front-end tick; confirm runs where the
 * input queue is read, before the page's script takes it. */
void black_FlashCheckpointItem(BlackFlashMenu *, uint32_t ui);
int black_FlashCheckpointConfirm(BlackFlashMenu *, uint32_t ui);
/* Profile options' Checkpoints row under Autosave: every front-end tick, and
 * its input (1 when taken) where the queue is read. */
void black_FlashCheckpointRow(BlackFlashMenu *, uint32_t ui);
int black_FlashCheckpointRowInput(BlackFlashMenu *, uint32_t ui, uint32_t input);
void black_FlashNameTick(BlackFlashMenu *, uint32_t ui);
int black_FlashPauseQuit(BlackFlashMenu *, uint32_t ui);
int black_FlashOnPage(BlackFlashMenu *, uint32_t ui, const char *name);
/* Host input, shared between the guest tick and Cocoa's main thread. */
void black_GameMenuState(int menu, int text);
int black_GameMenuActive(void);
int black_GameTextActive(void);
void black_GamePointerPosition(float x, float y, int inside);
void black_GamePointerButton(int down);
void black_GamePointerWheel(int up);
BlackFlashPointer black_GamePointerTake(void);
int black_GamePointerIntent(void);
void black_GameMenuConfirm(void);
int black_GameConfirmPending(void);
void black_GamePointerRelease(void);
void black_GameTextCharacter(unsigned code);
int black_GameTextTake(void);
int black_FramePointer(float x, float y, float width, float height, float aspect, float *nx, float *ny);
#endif
