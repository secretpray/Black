/* Production native-C HoldAim with checked synthetic guest addresses.
 * Checks stock scoped toggle, held/released scopes, non-zooming weapons,
 * missing data, keyboard ownership and adjacent memory preservation. */
#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static union { uint32_t words[1024]; uint8_t bytes[4096]; } guest;
static unsigned reads;
static uint8_t *address(uint32_t offset, unsigned size)
{
    if (offset + size > sizeof guest || offset < 0x100) abort();
    ++reads;
    return guest.bytes + offset;
}
#define MEM8(at) (*address(at, 1))
#define MEM32(at) (*(uint32_t *)address(at, 4))
#define MEMF(at) (*(float *)address(at, 4))
#include "../../ports/macos/overrides/game_aim.inc"
static int check(int wanted, int held, int expected)
{
    const unsigned controller = 0x100, weapon = 0x400;
    uint8_t before[sizeof guest];
    MEM8(controller + 0x45) = wanted; MEM8(controller + 0x104) = held;
    memcpy(before, &guest, sizeof before);
    black_HoldAim(controller, weapon);
    before[controller + 0x45] = expected;
    return memcmp(before, &guest, sizeof before) != 0;
}
int main(void)
{
    char path[] = "/tmp/black-controller-aim-XXXXXX";
    int fd = mkstemp(path); if (fd < 0) return 1; close(fd);
    setenv("BLACK_SETTINGS_FILE", path, 1);
    memset(&guest, 0xA5, sizeof guest);
    MEM32(0x400 + 0xEC) = 0x800; MEMF(0x800 + 0xB4) = 4.0f;
    if (black_SettingsHoldAim() || !black_ControllerAimAssist() || check(1, 0, 1) || check(0, 1, 0)) return 2;
    if (!black_SettingsSetHoldAim(1) || check(1, 0, 0) || check(0, 1, 1)) return 3;
    if (!black_SettingsSetHoldAim(0)) return 4;
    MEMF(0x800 + 0xB4) = 1.0f;
    if (check(1, 0, 0) || check(0, 1, 1)) return 5;
    MEM32(0x400 + 0xEC) = 0;
    if (check(1, 0, 0) || check(0, 1, 1)) return 6;
    black_SettingsSetHoldAim(1); black_InputSetKeyboardActive(1);
    if (black_ControllerAimAssist() || check(1, 0, 0) || check(0, 1, 1)) return 7;
    MEM32(0x400 + 0xEC) = 0x800; MEMF(0x800 + 0xB4) = 4;
    black_SettingsSetMouseHoldAim(0);
    if (check(1, 0, 1) || check(0, 1, 0)) return 8;
    black_InputSetKeyboardActive(0);
    if (!black_ControllerAimAssist() || !black_SettingsSetAimAssist(0) || black_ControllerAimAssist()) return 9;
    black_SettingsResetInput();
    if (black_SettingsHoldAim() || !black_ControllerAimAssist()) return 10;
    unlink(path);
    puts("TGP aim: stock scope toggle, held/released aim, non-zooming weapon, independent keyboard hold, adjacent guest memory: PASS");
    return 0;
}
