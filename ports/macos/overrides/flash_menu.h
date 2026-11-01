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
} BlackFlashApi;
typedef struct {
    BlackFlashApi api;
    uint32_t button, values, value, then, page, scratch;
    float seen_x, seen_y;
    unsigned reads, calls, name_gate;
    int fault, seen_pointer;
    char logged_name[64];
} BlackFlashMenu;
void black_FlashMenuTick(BlackFlashMenu *, uint32_t ui, BlackFlashPointer);
void black_FlashNameTick(BlackFlashMenu *, uint32_t ui);
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
