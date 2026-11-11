#include "../../ports/macos/overrides/port_menu.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/game_mods.h"
#include "../../ports/macos/overrides/game_checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int child(const char *binary, const char *mode, const char *path)
{
    pid_t pid = fork();
    if (!pid) { execl(binary, binary, mode, path, NULL); _exit(100); }
    int result;
    if (pid < 0 || waitpid(pid, &result, 0) < 0 || !WIFEXITED(result)) return 99;
    return WEXITSTATUS(result);
}
int main(int argc, char **argv)
{
    if (argc == 3) {
        setenv("BLACK_SETTINGS_FILE", argv[2], 1);
        if (!strcmp(argv[1], "fail"))
            return black_SettingsSetSkipIntros(1) || !black_SettingsSaveFailed() || !black_SettingsSkipIntros();
        if (!strcmp(argv[1], "mission")) return !black_SettingsSkipMissionIntros();
        if (!strcmp(argv[1], "checkpoints")) return !black_SettingsSaveCheckpoints();
        if (!strcmp(argv[1], "mods")) return !black_SettingsMod(BLACK_MOD_GOD_MODE) || !black_SettingsMod(BLACK_MOD_BFG) ||
            black_SettingsMod(BLACK_MOD_AMMO) || black_SettingsMod(-1) || black_SettingsMod(BLACK_MOD_COUNT) ||
            black_SettingsRenderScale() != 3;
        if (!strcmp(argv[1], "language")) return black_SettingsGameLanguage() != BLACK_LANG_GERMAN ||
            black_SettingsMenuLanguage() != 6;
        if (!strcmp(argv[1], "fps")) return black_SettingsPerformanceOverlay() != BLACK_PERF_FRAMES || black_SettingsRenderScale() != 3;
        if (!strcmp(argv[1], "textures")) return !black_SettingsTextureReplacement() || black_SettingsRenderScale() != 3;
        if (!strcmp(argv[1], "texture-size")) return black_SettingsTextureMaxSize() != 1024;
        if (!strcmp(argv[1], "reticle")) return !black_SettingsHdReticle();
        if (!strcmp(argv[1], "reticle-defaults")) return black_SettingsHdReticle();
        if (!strcmp(argv[1], "display")) return black_SettingsDisplayBrightness() != -7 || black_SettingsDisplayGamma() != 125;
        if (!strcmp(argv[1], "display-defaults")) return black_SettingsDisplayBrightness() != 0 || black_SettingsDisplayGamma() != 100;
        if (!strcmp(argv[1], "window")) { int f[4];
            return !black_SettingsWindowFrame(f) || f[0] != -40 || f[1] != 120 || f[2] != 1280 || f[3] != 720 ||
                black_SettingsWindowFullScreen() != 0; }
        if (!strcmp(argv[1], "window-defaults")) { int f[4];
            return black_SettingsWindowFrame(f) || black_SettingsWindowFullScreen() != -1; }
        if (!strcmp(argv[1], "mods-defaults")) {
            for (int i = 0; i < BLACK_MOD_COUNT; ++i) if (black_SettingsMod(i)) return 1;
            return black_SettingsSkipIntros() || black_SettingsSkipMissionIntros();
        }
        if (!strcmp(argv[1], "graphics")) return black_SettingsRenderScale() != 3 || black_SettingsFov() != 85;
        if (!strcmp(argv[1], "defaults")) return black_SettingsRenderScale() != 2 || black_SettingsFov() != 70 ||
            black_SettingsAntiAliasing() != 2 || black_SettingsAniso() != 0 || black_SettingsUpscaler() != 1;
        if (!strcmp(argv[1], "filtering")) return black_SettingsAntiAliasing() != 2 || black_SettingsAniso() != 8 ||
            black_SettingsRenderScale() != 3 || black_SettingsUpscaler() != 1;
        if (!strcmp(argv[1], "input")) return black_SettingsStickDeadzone(0) != 8 || black_SettingsStickDeadzone(1) != 4 ||
            black_SettingsTriggerDeadzone() != 2 || black_SettingsVibration() != 0 || black_SettingsDualSenseHid() != 0 ||
            black_SettingsRenderScale() != 3 || black_SettingsFov() != 85;
        if (!strcmp(argv[1], "look")) return black_SettingsLookSpeed(0) != 25 || black_SettingsLookSpeed(1) != 75 ||
            black_SettingsLookSpeed(2) != 35 || black_SettingsLookSpeed(3) != 40 ||
            !black_SettingsInvertX() || black_SettingsRumbleIntensity() != 120 ||
            !black_SettingsHoldAim() || black_SettingsAimAssist();
        if (!strcmp(argv[1], "input-defaults")) return black_SettingsStickDeadzone(0) != -1 ||
            black_SettingsStickDeadzone(1) != -1 || black_SettingsTriggerDeadzone() ||
            !black_SettingsVibration() || !black_SettingsDualSenseHid() ||
            black_SettingsLookSpeed(0) != 50 || black_SettingsLookSpeed(1) != 50 ||
            black_SettingsLookSpeed(2) != 50 || black_SettingsLookSpeed(3) != 50 ||
            black_SettingsInvertX() || black_SettingsRumbleIntensity() != 100 ||
            black_SettingsHoldAim() || !black_SettingsAimAssist();
        if (!strcmp(argv[1], "keyboard")) return black_SettingsMouseSpeed(0)!=25 || black_SettingsMouseSpeed(1)!=75 ||
            black_SettingsMouseSpeed(2)!=35 || black_SettingsMouseSpeed(3)!=40 || !black_SettingsMouseInvert(0) ||
            !black_SettingsMouseInvert(1) || black_SettingsMouseHoldAim() || black_SettingsWalkSpeed()!=35 ||
            black_SettingsBinding(0,0)!=13 || black_SettingsBinding(0,1)!=35 || black_SettingsBinding(6,0)!=BLACK_MOUSE_MIDDLE;
        if (!strcmp(argv[1], "keyboard-defaults")) {
            for(int i=0;i<4;++i) if(black_SettingsMouseSpeed(i)!=50)return 1;
            for(int i=0;i<BLACK_ACTION_COUNT;++i) for(int slot=0;slot<2;++slot)
                if(black_SettingsBinding(i,slot)!=black_KeyActions()[i].keys[slot])return 2;
            return black_SettingsMouseInvert(0)||black_SettingsMouseInvert(1)||!black_SettingsMouseHoldAim()||black_SettingsWalkSpeed()!=50;
        }
        if (!strcmp(argv[1], "input-cli")) {
            if (black_SettingsStickThreshold(0) != 3200 || black_SettingsStickThreshold(1) != 3200) return 1;
            if (!black_SettingsSetStickDeadzone(1, 0)) return 2;
            return black_SettingsStickThreshold(0) != 3200 || black_SettingsStickThreshold(1) != 0;
        }
        return black_SettingsSkipIntros() != atoi(argv[1]);
    }
    unsetenv("BLACK_SKIP_INTROS");
    char directory[] = "/tmp/black-settings-XXXXXX", path[512], invalid[512];
    if (!mkdtemp(directory)) return 1;
    snprintf(path, sizeof path, "%s/settings.ini", directory);
    snprintf(invalid, sizeof invalid, "%s/missing/settings.ini", directory);
    setenv("BLACK_SETTINGS_FILE", path, 1);
    if (black_SettingsSkipIntros()) return 2;
    if (!black_SettingsSetSkipIntros(1) || child(argv[0], "1", path)) return 3;
    if (!black_SettingsSetSkipMissionIntros(1) || child(argv[0], "mission", path)) return 11;
    if (!black_SettingsSetSkipIntros(0) || child(argv[0], "0", path)) return 4;
    if (child(argv[0], "mission", path) || !black_SettingsSetSkipMissionIntros(0)) return 12;
    /* Checkpoint saving is on until switched off, then stays off. */
    if (child(argv[0], "checkpoints", path) || !black_SettingsSetSaveCheckpoints(0) ||
        !child(argv[0], "checkpoints", path) || !black_SettingsSetSaveCheckpoints(1) ||
        child(argv[0], "checkpoints", path)) return 39;
    /* They follow the game's own Autosave: unknown until read, then its value. */
    if (black_CheckpointAutosave() != -1 || !black_CheckpointsActive()) return 40;
    black_CheckpointSetAutosave(0);
    if (black_CheckpointsActive() || black_CheckpointResumeOffer() || black_CheckpointTakeRestart(1)) return 41;
    black_CheckpointSetAutosave(1);
    if (!black_CheckpointsActive() || !black_SettingsSetSaveCheckpoints(0) || black_CheckpointsActive() ||
        !black_SettingsSetSaveCheckpoints(1)) return 42;
    if (!black_SettingsSetRenderScale(3) || !black_SettingsSetFov(85) || child(argv[0], "graphics", path)) return 13;
    if (black_SettingsSetRenderScale(0) || black_SettingsSetRenderScale(7) ||
        black_SettingsSetFov(44) || black_SettingsSetFov(141) || child(argv[0], "graphics", path)) return 14;
    if (!black_SettingsSetSkipIntros(1) || child(argv[0], "graphics", path) || !black_SettingsSetSkipIntros(0)) return 15;
    if (!black_SettingsSetAntiAliasing(2) || !black_SettingsSetAniso(8) || !black_SettingsSetUpscaler(1) ||
        child(argv[0], "filtering", path)) return 17;
    if (black_SettingsSetAntiAliasing(3) || black_SettingsSetAniso(3) || black_SettingsSetAniso(32) ||
        black_SettingsSetUpscaler(2) ||
        child(argv[0], "filtering", path)) return 18;
    if (child(argv[0], "input-defaults", path)) return 19;
    if (!black_SettingsSetStickDeadzone(0, 8) || !black_SettingsSetStickDeadzone(1, 4) ||
        !black_SettingsSetTriggerDeadzone(2) || !black_SettingsSetVibration(0) ||
        !black_SettingsSetDualSenseHid(0) || child(argv[0], "input", path)) return 20;
    if (black_SettingsSetStickDeadzone(0, -2) || black_SettingsSetStickDeadzone(1, 51) ||
        black_SettingsSetTriggerDeadzone(51) || child(argv[0], "input", path)) return 21;
    if (!black_SettingsSetLookSpeed(0, 25) || !black_SettingsSetLookSpeed(1, 75) ||
        !black_SettingsSetLookSpeed(2, 35) || !black_SettingsSetLookSpeed(3, 40) ||
        !black_SettingsSetInvertX(1) || !black_SettingsSetRumbleIntensity(120) ||
        !black_SettingsSetHoldAim(1) || !black_SettingsSetAimAssist(0) || child(argv[0], "look", path)) return 24;
    setenv("RECOMP_PAD_DEADZONE", "3200", 1);
    if (child(argv[0], "input-cli", path)) return 22;
    unsetenv("RECOMP_PAD_DEADZONE");
    if (!black_SettingsResetInput() || child(argv[0], "input-defaults", path) ||
        child(argv[0], "filtering", path)) return 23;
    if(child(argv[0],"keyboard-defaults",path))return 25;
    int mouse_values[]={25,75,35,40};for(int i=0;i<4;++i)if(!black_SettingsSetMouseSpeed(i,mouse_values[i]))return 26;
    if(!black_SettingsSetMouseInvert(0,1)||!black_SettingsSetMouseInvert(1,1)||!black_SettingsSetMouseHoldAim(0)||
        !black_SettingsSetWalkSpeed(35)||!black_SettingsSetBinding(0,1,35)||!black_SettingsSetBinding(6,0,BLACK_MOUSE_MIDDLE)||
        child(argv[0],"keyboard",path)||child(argv[0],"input-defaults",path))return 27;
    if(black_SettingsSetMouseSpeed(0,0)||black_SettingsSetMouseSpeed(0,201)||black_SettingsSetWalkSpeed(0)||
        black_SettingsSetBinding(0,1,-2)||black_SettingsSetBinding(0,1,109)||black_SettingsSetBinding(24,1,35)||
        child(argv[0],"keyboard",path))return 28;
    if(!black_SettingsResetInput()||child(argv[0],"keyboard",path)||!black_SettingsResetKeyboard()||
        child(argv[0],"keyboard-defaults",path)||child(argv[0],"filtering",path))return 29;
    if (child(argv[0], "mods-defaults", path)) return 30;
    if (!black_SettingsSetGameLanguage(BLACK_LANG_GERMAN) || !black_SettingsSetMenuLanguage(6) ||
        black_SettingsSetGameLanguage(BLACK_GAME_LANGUAGES) || black_SettingsSetMenuLanguage(7) || black_SettingsSetGameLanguage(-1) ||
        child(argv[0], "language", path) || !black_SettingsSetGameLanguage(0) || !black_SettingsSetMenuLanguage(0)) return 34;
    if (!black_SettingsSetPerformanceOverlay(BLACK_PERF_FRAMES) || child(argv[0], "fps", path) ||
        black_SettingsSetPerformanceOverlay(3) || !black_SettingsSetPerformanceOverlay(BLACK_PERF_OFF) ||
        !child(argv[0], "fps", path)) return 33;
    /* Texture replacement is off until chosen, then saved like the others. */
    if (child(argv[0], "textures", path) != 1 || !black_SettingsSetTextureReplacement(1) ||
        child(argv[0], "textures", path) || !black_SettingsSetTextureReplacement(0) ||
        !child(argv[0], "textures", path)) return 35;
    /* HD texture size: 2048 until chosen; only 1024, 2048 and 4096. */
    if (!child(argv[0], "texture-size", path) || black_SettingsSetTextureMaxSize(3000) ||
        !black_SettingsSetTextureMaxSize(1024) || child(argv[0], "texture-size", path) ||
        !black_SettingsSetTextureMaxSize(2048)) return 36;
    /* HD reticle: Original until chosen, then saved like the others. */
    if (child(argv[0], "reticle-defaults", path)) return 43;
    if (!black_SettingsSetHdReticle(1)) return 44;
    if (child(argv[0], "reticle", path)) return 45;
    if (!black_SettingsSetHdReticle(0)) return 46;
    if (child(argv[0], "reticle-defaults", path)) return 47;
    /* Display brightness/gamma: neutral until chosen, live range only. */
    if (child(argv[0], "display-defaults", path) || black_SettingsSetDisplayBrightness(21) ||
        black_SettingsSetDisplayBrightness(-21) || black_SettingsSetDisplayGamma(59) || black_SettingsSetDisplayGamma(161) ||
        !black_SettingsSetDisplayBrightness(-7) || !black_SettingsSetDisplayGamma(125) || child(argv[0], "display", path) ||
        !black_SettingsSetDisplayBrightness(0) || !black_SettingsSetDisplayGamma(100) ||
        child(argv[0], "display-defaults", path)) return 38;
    /* Window state: never saved until the window reports one; bad rects refused. */
    { int bad[4] = {0, 0, 100, 720}, good[4] = {-40, 120, 1280, 720};
      if (child(argv[0], "window-defaults", path) || black_SettingsSetWindowState(bad, 0) ||
          black_SettingsSetWindowState(good, 2) || !black_SettingsSetWindowState(good, 1) ||
          !black_SettingsSetWindowState(NULL, 0) || child(argv[0], "window", path) ||
          child(argv[0], "texture-size", path) == 0) return 37; }
    if (!black_SettingsSetMod(BLACK_MOD_GOD_MODE, 1) || !black_SettingsSetMod(BLACK_MOD_BFG, 5) ||
        black_SettingsSetMod(BLACK_MOD_COUNT, 1) || black_SettingsSetMod(-1, 1) || child(argv[0], "mods", path)) return 31;
    if (!black_SettingsSetSkipIntros(1) || !black_SettingsResetMods() || child(argv[0], "mods-defaults", path)) return 32;
    setenv("BLACK_SKIP_INTROS", "1", 1);
    if (child(argv[0], "1", path)) return 5;
    unsetenv("BLACK_SKIP_INTROS");
    if (child(argv[0], "0", path)) return 6;
    if (child(argv[0], "fail", invalid)) return 7;
    FILE *file = fopen(path, "w");
    if (!file) return 8;
    fputs("skip_intro_movies=broken\nrender_scale=7\nfield_of_view=45oops\nrender_scale=3 extra\n"
          "anti_aliasing=5\nanisotropic_filtering=6\nsave_checkpoints=maybe\ncontroller_sensitivity=0\ncontroller_vertical_sensitivity=201\nrumble_intensity=-1\ninvert_controller_x=3\ncontroller_hold_aim=-1\naim_assist=2\nmouse_sensitivity=0\nmouse_vertical_sensitivity=201\nwalk_speed=101\nkey_move_forward_1=-2\nkey_fire_1=109\nmod_god_mode=2\nmod_bfg=on\nwindow_frame=1,2,50,50\nwindow_full_screen=3\ndisplay_brightness=30\ndisplay_gamma=1.2\nhd_reticle=2\n", file); fclose(file);
    if (child(argv[0], "0", path)) return 9;
    if (child(argv[0], "defaults", path) || child(argv[0], "input-defaults", path) || child(argv[0],"keyboard-defaults",path) ||
        child(argv[0], "mods-defaults", path) || child(argv[0], "window-defaults", path) ||
        child(argv[0], "display-defaults", path) || child(argv[0], "reticle-defaults", path)) return 16;
    black_MenuCommand(BLACK_MENU_CHANGE);
    black_MenuCommand(BLACK_MENU_ON);
    if (black_MenuTakeCommands() != (BLACK_MENU_CHANGE | BLACK_MENU_ON) || black_MenuTakeCommands()) return 10;
    unlink(path); rmdir(directory);
    puts("Settings default, atomic persistence, restart, CLI override and save failure: PASS");
    return 0;
}
