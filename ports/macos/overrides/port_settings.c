/* Small persisted port preferences, separate from Xbox saves and game data. */
#include "port_menu.h"
#include "keyboard_mouse.h"
#include "input_profile.h"
#include "game_mods.h"
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static pthread_once_t initialized = PTHREAD_ONCE_INIT;
static pthread_mutex_t save_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int skip_intros, skip_missions, skip_any_film = 1, save_failed, menu_open;
static atomic_int render_scale = 2, field_of_view = 70;
/* anti_aliasing: 0 off, 1 FXAA, 2 SMAA. anisotropic_filtering: 0 keeps the
 * game's own level, else 2, 4, 8 or 16. */
static atomic_int anti_aliasing = 2, anisotropic_filtering;
/* upscaler: 0 bilinear presentation, 1 MetalFX spatial. Defaults SMAA + MetalFX:
 * measured 31.10, SMAA under 0.1 ms and MetalFX about 4 ms of GPU in the 60 Hz
 * presentation pass, with no effect on the game's own frame rate. */
static atomic_int upscaler = 1;
static atomic_int fps_counter; /* TGP's FPS COUNTER: the game's frame rate, top left */
/* HD textures from the user's pack and the shipped map (texture_pack.h); live. */
static atomic_int texture_replacement;
/* Largest side of a replacement: 1024, 2048 or 4096; larger files lose top levels. */
static atomic_int texture_max_size = 2048;
/* game_language: 0 English, 1 French, 2 German, 3 Spanish, 4 Italian, 5 Russian (next launch).
 * menu_language: 0 follows the game, then English, French, German, Spanish,
 * Italian, Russian. */
static atomic_int game_language, menu_language;
static atomic_int stick_deadzone[2] = {-1, -1}; /* -1 keeps BLACK's original 20% */
static atomic_int stick_cli[2] = {-1, -1}; /* exact legacy CLI counts; UI edits clear */
static atomic_int trigger_deadzone, vibration = 1, dualsense_hid = 1;
static atomic_int look_speed[4] = {50, 50, 50, 50};
static atomic_int invert_x, rumble_intensity = 100, keyboard_active;
static atomic_int hold_aim, aim_assist = 1;
static atomic_int mouse_speed[4] = {50,50,50,50}, mouse_invert[2], mouse_hold_aim = 1, walk_speed = 50;
static atomic_int bindings[BLACK_ACTION_COUNT][2];
static atomic_int input_device, input_cli = -1, button_prompts;
static atomic_int pad_bindings[BLACK_PAD_CONTROLS];
static atomic_int mods[BLACK_MOD_COUNT], mods_supported = -1;
static _Atomic(const char *) mods_revision = "DETECTING";
static atomic_uint menu_commands;
static char *settings_path;

static void load_settings(void)
{
    for (int i = 0; i < BLACK_PAD_CONTROLS; ++i) atomic_store(&pad_bindings[i], i);
    const BlackKeyAction *actions = black_KeyActions();
    for (int i = 0; i < BLACK_ACTION_COUNT; ++i)
        for (int slot = 0; slot < 2; ++slot) atomic_store(&bindings[i][slot], actions[i].keys[slot]);
    const char *path = getenv("BLACK_SETTINGS_FILE");
    settings_path = strdup(path ? path : "black-settings.ini");
    FILE *file = settings_path ? fopen(settings_path, "r") : NULL;
    if (file) {
        char line[512];
        while (fgets(line, sizeof line, file)) {
            char value[16], extra;
            if (sscanf(line, " skip_intro_movies = %15s %c", value, &extra) == 1) {
                if (!strcmp(value, "1") || !strcmp(value, "true")) atomic_store(&skip_intros, 1);
                else if (!strcmp(value, "0") || !strcmp(value, "false")) atomic_store(&skip_intros, 0);
            }
            if (sscanf(line, " skip_mission_intros = %15s %c", value, &extra) == 1) {
                if (!strcmp(value, "1") || !strcmp(value, "true")) atomic_store(&skip_missions, 1);
                else if (!strcmp(value, "0") || !strcmp(value, "false")) atomic_store(&skip_missions, 0);
            }
            if (sscanf(line, " skip_any_film = %15s %c", value, &extra) == 1) {
                if (!strcmp(value, "1") || !strcmp(value, "true")) atomic_store(&skip_any_film, 1);
                else if (!strcmp(value, "0") || !strcmp(value, "false")) atomic_store(&skip_any_film, 0);
            }
            char *end;
            const char *keys[] = {"left_stick_deadzone", "right_stick_deadzone",
                                  "trigger_deadzone", "vibration", "dualsense_hidapi",
                                  "controller_sensitivity", "controller_vertical_sensitivity",
                                  "controller_aim_horizontal_sensitivity", "controller_aim_vertical_sensitivity",
                                  "invert_controller_x", "rumble_intensity", "controller_hold_aim", "aim_assist"};
            atomic_int *fields[] = {&stick_deadzone[0], &stick_deadzone[1],
                                     &trigger_deadzone, &vibration, &dualsense_hid,
                                     &look_speed[0], &look_speed[1], &look_speed[2], &look_speed[3],
                                     &invert_x, &rumble_intensity, &hold_aim, &aim_assist};
            for (int i = 0; i < 13; ++i) {
                char format[80];
                snprintf(format, sizeof format, " %s = %%15s %%c", keys[i]);
                if (sscanf(line, format, value, &extra) == 1) {
                    long n = strtol(value, &end, 10);
                    int low = i < 2 ? -1 : i >= 5 && i <= 8 ? 1 : 0;
                    int high = i < 3 ? 50 : i >= 5 && i <= 8 ? 200 : i == 10 ? 200 : 1;
                    if (*value && !*end && n >= low && n <= high) atomic_store(fields[i], (int)n);
                }
            }
            const char *mouse_keys[] = {"mouse_sensitivity", "mouse_vertical_sensitivity",
                "aim_horizontal_sensitivity", "aim_vertical_sensitivity", "invert_mouse_x", "invert_mouse", "hold_aim", "walk_speed"};
            atomic_int *mouse_fields[] = {&mouse_speed[0],&mouse_speed[1],&mouse_speed[2],&mouse_speed[3],
                &mouse_invert[0],&mouse_invert[1],&mouse_hold_aim,&walk_speed};
            for (int i = 0; i < 8; ++i) {
                char format[80]; snprintf(format,sizeof format," %s = %%15s %%c",mouse_keys[i]);
                if (sscanf(line,format,value,&extra) == 1) {
                    long n = strtol(value,&end,10);
                    int low = i < 4 || i == 7 ? 1 : 0, high = i < 4 ? 200 : i == 7 ? 100 : 1;
                    if (*value && !*end && n >= low && n <= high) atomic_store(mouse_fields[i],(int)n);
                }
            }
            const char *input_keys[] = {"input_device", "button_prompts"};
            atomic_int *input_fields[] = {&input_device, &button_prompts};
            for (int i = 0; i < 2; ++i) {
                char format[80]; snprintf(format,sizeof format," %s = %%15s %%c",input_keys[i]);
                if (sscanf(line,format,value,&extra) == 1) {
                    long n = strtol(value,&end,10);
                    if (*value && !*end && n >= 0 && n <= (i ? 3 : 2)) atomic_store(input_fields[i],(int)n);
                }
            }
            for (int i = 0; i < BLACK_PAD_CONTROLS; ++i) {
                char format[80]; snprintf(format,sizeof format," pad_%s = %%15s %%c",black_PadControlId(i));
                if (sscanf(line,format,value,&extra) == 1) {
                    long n = strtol(value,&end,10);
                    if (*value && !*end && n >= -1 && n < BLACK_PAD_CONTROLS) atomic_store(&pad_bindings[i],(int)n);
                }
            }
            for (int i = 0; i < BLACK_ACTION_COUNT; ++i) for (int slot = 0; slot < 2; ++slot) {
                char format[96]; snprintf(format,sizeof format," key_%s_%d = %%15s %%c",actions[i].id,slot+1);
                if (sscanf(line,format,value,&extra) == 1) {
                    long n = strtol(value,&end,10);
                    if (*value && !*end && n >= -1 && n < BLACK_KEY_LIMIT && black_KeyValid((int)n))
                        atomic_store(&bindings[i][slot],black_KeyCanonical((int)n));
                }
            }
            for (int i = 0; i < BLACK_MOD_COUNT; ++i) {
                char format[96]; snprintf(format, sizeof format, " %s = %%15s %%c", black_mod_info[i].key);
                if (sscanf(line, format, value, &extra) == 1) {
                    long n = strtol(value, &end, 10);
                    if (!strcmp(value, "true")) atomic_store(&mods[i], 1);
                    else if (!strcmp(value, "false")) atomic_store(&mods[i], 0);
                    else if (*value && !*end && n >= 0 && n <= black_mod_info[i].levels) atomic_store(&mods[i], (int)n);
                }
            }
            if (sscanf(line, " render_scale = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 1 && n <= 6) atomic_store(&render_scale, (int)n);
            }
            if (sscanf(line, " field_of_view = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 45 && n <= 140) atomic_store(&field_of_view, (int)n);
            }
            if (sscanf(line, " anti_aliasing = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 0 && n <= 2) atomic_store(&anti_aliasing, (int)n);
            }
            if (sscanf(line, " game_language = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 0 && n < BLACK_GAME_LANGUAGES) atomic_store(&game_language, (int)n);
            }
            if (sscanf(line, " menu_language = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 0 && n <= 6) atomic_store(&menu_language, (int)n);
            }
            if (sscanf(line, " fps_counter = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 0 && n <= 1) atomic_store(&fps_counter, (int)n);
            }
            if (sscanf(line, " texture_replacement = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 0 && n <= 1) atomic_store(&texture_replacement, (int)n);
            }
            if (sscanf(line, " texture_max_size = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && (n == 1024 || n == 2048 || n == 4096)) atomic_store(&texture_max_size, (int)n);
            }
            if (sscanf(line, " upscaler = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && n >= 0 && n <= 1) atomic_store(&upscaler, (int)n);
            }
            if (sscanf(line, " anisotropic_filtering = %15s %c", value, &extra) == 1) {
                long n = strtol(value, &end, 10);
                if (*value && !*end && black_SettingsValidAniso((int)n)) atomic_store(&anisotropic_filtering, (int)n);
            }
        }
        fclose(file);
    }
    const char *device = getenv("BLACK_INPUT_DEVICE");
    if (device && (!strcmp(device,"auto") || !strcmp(device,"gamepad") || !strcmp(device,"keyboard")))
        atomic_store(&input_cli,!strcmp(device,"gamepad") ? BLACK_INPUT_GAMEPAD : !strcmp(device,"keyboard") ? BLACK_INPUT_KEYBOARD : BLACK_INPUT_AUTO);
    /* CLI override is a starting value for this run. UI edits can turn it off. */
    if (getenv("BLACK_SKIP_INTROS")) atomic_store(&skip_intros, 1);
    const char *pad = getenv("RECOMP_PAD_DEADZONE");
    if (pad) {
        char *end;
        long n = strtol(pad, &end, 10);
        if (*pad && !*end && n >= 0 && n <= 16000) {
            for (int i = 0; i < 2; ++i) {
                atomic_store(&stick_cli[i], (int)n);
            }
        }
    }
}

int black_SettingsSkipIntros(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&skip_intros);
}

int black_SettingsSkipMissionIntros(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&skip_missions);
}

/* A, START or Enter skips any film, also one the game does not let you skip
 * yet; on by default. */
int black_SettingsSkipAnyFilm(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&skip_any_film);
}

int black_SettingsRenderScale(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&render_scale);
}

int black_SettingsFov(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&field_of_view);
}

int black_SettingsAntiAliasing(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&anti_aliasing);
}

int black_SettingsAniso(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&anisotropic_filtering);
}

int black_SettingsUpscaler(void)
{
    pthread_once(&initialized, load_settings);
    return atomic_load(&upscaler);
}

int black_SettingsValidAniso(int level)
{
    return level == 0 || level == 2 || level == 4 || level == 8 || level == 16;
}

static int save_setting(atomic_int *setting, int value)
{
    pthread_once(&initialized, load_settings);
    pthread_mutex_lock(&save_lock);
    atomic_store(setting, value);
    int success = 0;
    char *temporary = NULL;
    if (settings_path && asprintf(&temporary, "%s.XXXXXX", settings_path) >= 0) {
        int fd = mkstemp(temporary);
        if (fd >= 0) {
            FILE *file = fdopen(fd, "w");
            if (file) {
                int written = fprintf(file, "# BLACK recomp settings\nskip_intro_movies=%d\nskip_mission_intros=%d\nskip_any_film=%d\nrender_scale=%d\nfield_of_view=%d\nanti_aliasing=%d\nanisotropic_filtering=%d\nupscaler=%d\nleft_stick_deadzone=%d\nright_stick_deadzone=%d\ntrigger_deadzone=%d\nvibration=%d\ndualsense_hidapi=%d\n# Controller sensitivity units: 50 = original 1x\ncontroller_sensitivity=%d\ncontroller_vertical_sensitivity=%d\ncontroller_aim_horizontal_sensitivity=%d\ncontroller_aim_vertical_sensitivity=%d\ninvert_controller_x=%d\nrumble_intensity=%d\ncontroller_hold_aim=%d\naim_assist=%d\n",
                    atomic_load(&skip_intros), atomic_load(&skip_missions), atomic_load(&skip_any_film),
                    atomic_load(&render_scale), atomic_load(&field_of_view),
                    atomic_load(&anti_aliasing), atomic_load(&anisotropic_filtering),
                    atomic_load(&upscaler), atomic_load(&stick_deadzone[0]), atomic_load(&stick_deadzone[1]),
                    atomic_load(&trigger_deadzone), atomic_load(&vibration), atomic_load(&dualsense_hid),
                    atomic_load(&look_speed[0]), atomic_load(&look_speed[1]), atomic_load(&look_speed[2]),
                    atomic_load(&look_speed[3]), atomic_load(&invert_x), atomic_load(&rumble_intensity),
                    atomic_load(&hold_aim), atomic_load(&aim_assist)) > 0;
                written &= fprintf(file, "# Mouse sensitivity units: 50 = 1x\nmouse_sensitivity=%d\nmouse_vertical_sensitivity=%d\naim_horizontal_sensitivity=%d\naim_vertical_sensitivity=%d\ninvert_mouse_x=%d\ninvert_mouse=%d\nhold_aim=%d\nwalk_speed=%d\n",
                    atomic_load(&mouse_speed[0]),atomic_load(&mouse_speed[1]),atomic_load(&mouse_speed[2]),atomic_load(&mouse_speed[3]),
                    atomic_load(&mouse_invert[0]),atomic_load(&mouse_invert[1]),atomic_load(&mouse_hold_aim),atomic_load(&walk_speed)) > 0;
                written &= fprintf(file,"input_device=%d\nbutton_prompts=%d\n",atomic_load(&input_device),atomic_load(&button_prompts)) > 0;
                for (int i = 0; i < BLACK_PAD_CONTROLS; ++i)
                    written &= fprintf(file,"pad_%s=%d\n",black_PadControlId(i),atomic_load(&pad_bindings[i])) > 0;
                const BlackKeyAction *actions = black_KeyActions();
                for (int i = 0; i < BLACK_ACTION_COUNT; ++i) for (int slot = 0; slot < 2; ++slot)
                    written &= fprintf(file,"key_%s_%d=%d\n",actions[i].id,slot+1,atomic_load(&bindings[i][slot])) > 0;
                written &= fprintf(file, "fps_counter=%d\ngame_language=%d\nmenu_language=%d\ntexture_replacement=%d\ntexture_max_size=%d\n# Mods page: 1 on, 0 off; Rapid Fire 0-3, Game Speed 0-4 (0 the game's own)\n",
                    atomic_load(&fps_counter), atomic_load(&game_language), atomic_load(&menu_language),
                    atomic_load(&texture_replacement), atomic_load(&texture_max_size)) > 0;
                for (int i = 0; i < BLACK_MOD_COUNT; ++i)
                    written &= fprintf(file, "%s=%d\n", black_mod_info[i].key, atomic_load(&mods[i])) > 0;
                int flushed = fflush(file) == 0 && fsync(fd) == 0;
                int closed = fclose(file) == 0;
                if (written && flushed && closed && rename(temporary, settings_path) == 0) success = 1;
            } else close(fd);
            if (!success) unlink(temporary);
        }
        free(temporary);
    }
    atomic_store(&save_failed, !success);
    fprintf(stderr, "[SETTINGS] skip_intro_movies=%d skip_mission_intros=%d render_scale=%d field_of_view=%d anti_aliasing=%d anisotropic_filtering=%d upscaler=%d %s\n",
        atomic_load(&skip_intros), atomic_load(&skip_missions),
        atomic_load(&render_scale), atomic_load(&field_of_view),
        atomic_load(&anti_aliasing), atomic_load(&anisotropic_filtering), atomic_load(&upscaler),
        success ? "saved" : "could not save, session only");
    fprintf(stderr, "[SETTINGS-INPUT] left=%d right=%d trigger=%d vibration=%d dualsense_hidapi=%d look=%d/%d aim=%d/%d invert_x=%d rumble=%d%% hold_aim=%d aim_assist=%d\n",
        atomic_load(&stick_deadzone[0]), atomic_load(&stick_deadzone[1]), atomic_load(&trigger_deadzone),
        atomic_load(&vibration), atomic_load(&dualsense_hid), atomic_load(&look_speed[0]), atomic_load(&look_speed[1]),
        atomic_load(&look_speed[2]), atomic_load(&look_speed[3]), atomic_load(&invert_x), atomic_load(&rumble_intensity),
        atomic_load(&hold_aim), atomic_load(&aim_assist));
    pthread_mutex_unlock(&save_lock);
    return success;
}

int black_SettingsSetSkipIntros(int enabled) { return save_setting(&skip_intros, !!enabled); }
int black_SettingsSetSkipMissionIntros(int enabled) { return save_setting(&skip_missions, !!enabled); }
int black_SettingsSetSkipAnyFilm(int enabled) { return save_setting(&skip_any_film, !!enabled); }
int black_SettingsSetRenderScale(int scale)
{ return scale >= 1 && scale <= 6 ? save_setting(&render_scale, scale) : 0; }
int black_SettingsSetFov(int degrees)
{ return degrees >= 45 && degrees <= 140 ? save_setting(&field_of_view, degrees) : 0; }
int black_SettingsSetAntiAliasing(int mode)
{ return mode >= 0 && mode <= 2 ? save_setting(&anti_aliasing, mode) : 0; }
int black_SettingsSetUpscaler(int mode)
{ return mode >= 0 && mode <= 1 ? save_setting(&upscaler, mode) : 0; }
int black_SettingsFpsCounter(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&fps_counter); }
int black_SettingsSetFpsCounter(int enabled) { return save_setting(&fps_counter, !!enabled); }
int black_SettingsTextureReplacement(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&texture_replacement); }
int black_SettingsSetTextureReplacement(int enabled) { return save_setting(&texture_replacement, !!enabled); }
int black_SettingsTextureMaxSize(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&texture_max_size); }
int black_SettingsSetTextureMaxSize(int size)
{ return size == 1024 || size == 2048 || size == 4096 ? save_setting(&texture_max_size, size) : 0; }
int black_SettingsGameLanguage(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&game_language); }
int black_SettingsSetGameLanguage(int language)
{ return language >= 0 && language < BLACK_GAME_LANGUAGES ? save_setting(&game_language, language) : 0; }
int black_SettingsMenuLanguage(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&menu_language); }
int black_SettingsSetMenuLanguage(int language)
{ return language >= 0 && language <= 6 ? save_setting(&menu_language, language) : 0; }
int black_SettingsSetAniso(int level)
{ return black_SettingsValidAniso(level) ? save_setting(&anisotropic_filtering, level) : 0; }

int black_SettingsStickDeadzone(int right)
{
    pthread_once(&initialized, load_settings);
    int override = atomic_load(&stick_cli[!!right]);
    if (override >= 0) return override ? (override * 100 + 16383) / 32767 : -1;
    return atomic_load(&stick_deadzone[!!right]);
}
int black_SettingsStickThreshold(int right)
{
    pthread_once(&initialized, load_settings);
    int override = atomic_load(&stick_cli[!!right]);
    if (override >= 0) return override ? override : -1;
    int percent = atomic_load(&stick_deadzone[!!right]);
    return percent < 0 ? -1 : (percent * 32767 + 50) / 100;
}
int black_SettingsSetStickDeadzone(int right, int percent)
{
    if (percent < -1 || percent > 50) return 0;
    pthread_once(&initialized, load_settings);
    atomic_store(&stick_cli[!!right], -1);
    return save_setting(&stick_deadzone[!!right], percent);
}
int black_SettingsTriggerDeadzone(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&trigger_deadzone); }
int black_SettingsSetTriggerDeadzone(int percent)
{ return percent >= 0 && percent <= 50 ? save_setting(&trigger_deadzone, percent) : 0; }
int black_SettingsVibration(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&vibration); }
int black_SettingsSetVibration(int enabled) { return save_setting(&vibration, !!enabled); }
int black_SettingsDualSenseHid(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&dualsense_hid); }
int black_SettingsSetDualSenseHid(int enabled) { return save_setting(&dualsense_hid, !!enabled); }
int black_SettingsResetInput(void)
{
    pthread_once(&initialized, load_settings);
    for (int i = 0; i < 2; ++i) { atomic_store(&stick_cli[i], -1); atomic_store(&stick_deadzone[i], -1); }
    atomic_store(&input_device, BLACK_INPUT_AUTO); atomic_store(&input_cli,-1); atomic_store(&button_prompts, BLACK_PROMPTS_AUTO);
    for (int i = 0; i < BLACK_PAD_CONTROLS; ++i) atomic_store(&pad_bindings[i],i);
    atomic_store(&trigger_deadzone, 0); atomic_store(&vibration, 1);
    for (int i = 0; i < 4; ++i) atomic_store(&look_speed[i], 50);
    atomic_store(&invert_x, 0); atomic_store(&rumble_intensity, 100);
    atomic_store(&hold_aim, 0); atomic_store(&aim_assist, 1);
    return save_setting(&dualsense_hid, 1);
}

int black_SettingsSaveFailed(void) { return atomic_load(&save_failed); }
/* Settings panel or the Home quit dialog: either one freezes the world and
 * keeps game input; the panel alone is "settings". */
static atomic_int quit_dialog, quit_to_menu;
int black_MenuIsOpen(void) { return atomic_load(&menu_open) || atomic_load(&quit_dialog); }
int black_SettingsPanelOpen(void) { return atomic_load(&menu_open); }
void black_MenuSetOpen(int open) { atomic_store(&menu_open, !!open); }
int black_QuitDialogOpen(void) { return atomic_load(&quit_dialog); }
void black_QuitDialogSetOpen(int open) { atomic_store(&quit_dialog, !!open); }
/* Quit to main menu: asked by the dialog, carried out on the game thread
 * (game_flash.c) through the pause menu's own quit confirmation. */
void black_QuitToMenuRequest(void) { atomic_store(&quit_to_menu, 1); }
int black_QuitToMenuTake(void) { return atomic_exchange(&quit_to_menu, 0); }
void black_MenuCommand(unsigned commands) { atomic_fetch_or(&menu_commands, commands); }
unsigned black_MenuTakeCommands(void) { return atomic_exchange(&menu_commands, 0); }

int black_SettingsLookSpeed(int axis)
{ pthread_once(&initialized, load_settings); return axis >= 0 && axis < 4 ? atomic_load(&look_speed[axis]) : 50; }
int black_SettingsSetLookSpeed(int axis, int units)
{ return axis >= 0 && axis < 4 && units >= 1 && units <= 200 ? save_setting(&look_speed[axis], units) : 0; }
int black_SettingsInvertX(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&invert_x); }
int black_SettingsSetInvertX(int enabled) { return save_setting(&invert_x, !!enabled); }
int black_SettingsRumbleIntensity(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&rumble_intensity); }
int black_SettingsSetRumbleIntensity(int percent)
{ return percent >= 0 && percent <= 200 ? save_setting(&rumble_intensity, percent) : 0; }
int black_InputKeyboardActive(void) { return atomic_load(&keyboard_active); }
static atomic_int input_focused = 1;
static atomic_uint input_focus_epoch;
void black_InputFocusChanged(int focused)
{
    focused = !!focused;
    int previous = atomic_exchange(&input_focused, focused);
    if (focused && !previous) atomic_fetch_add(&input_focus_epoch, 1);
    if (previous != focused && getenv("RECOMP_INPUT_DEBUG"))
        fprintf(stderr, "[INPUT-FOCUS] active=%d epoch=%u\n", focused, atomic_load(&input_focus_epoch));
}
unsigned black_InputFocusEpoch(void) { return atomic_load(&input_focus_epoch); }
int black_InputFocused(void) { return atomic_load(&input_focused); }
void black_InputSetKeyboardActive(int active)
{
    active = !!active;
    int previous = atomic_exchange(&keyboard_active, active);
    if (previous != active && getenv("RECOMP_INPUT_DEBUG"))
        fprintf(stderr, "[INPUT-OWNER] %s policy=%d\n", active ? "keyboard" : "gamepad", black_SettingsInputDevice());
}

int black_SettingsHoldAim(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&hold_aim); }
int black_SettingsSetHoldAim(int enabled) { return save_setting(&hold_aim, !!enabled); }
int black_SettingsAimAssist(void)
{ pthread_once(&initialized, load_settings); return atomic_load(&aim_assist); }
int black_SettingsSetAimAssist(int enabled) { return save_setting(&aim_assist, !!enabled); }

int black_SettingsMouseSpeed(int axis)
{ pthread_once(&initialized,load_settings); return axis >= 0 && axis < 4 ? atomic_load(&mouse_speed[axis]) : 50; }
int black_SettingsSetMouseSpeed(int axis,int units)
{ return axis >= 0 && axis < 4 && units >= 1 && units <= 200 ? save_setting(&mouse_speed[axis],units) : 0; }
int black_SettingsMouseInvert(int vertical)
{ pthread_once(&initialized,load_settings); return atomic_load(&mouse_invert[!!vertical]); }
int black_SettingsSetMouseInvert(int vertical,int enabled) { return save_setting(&mouse_invert[!!vertical],!!enabled); }
int black_SettingsMouseHoldAim(void)
{ pthread_once(&initialized,load_settings); return atomic_load(&mouse_hold_aim); }
int black_SettingsSetMouseHoldAim(int enabled) { return save_setting(&mouse_hold_aim,!!enabled); }
int black_SettingsWalkSpeed(void)
{ pthread_once(&initialized,load_settings); return atomic_load(&walk_speed); }
int black_SettingsSetWalkSpeed(int percent) { return percent >= 1 && percent <= 100 ? save_setting(&walk_speed,percent) : 0; }
int black_SettingsBinding(int action,int slot)
{ pthread_once(&initialized,load_settings); return action >= 0 && action < BLACK_ACTION_COUNT && slot >= 0 && slot < 2 ? atomic_load(&bindings[action][slot]) : -1; }
int black_SettingsSetBinding(int action,int slot,int key)
{
    if (action < 0 || action >= BLACK_ACTION_COUNT || slot < 0 || slot > 1 || !black_KeyValid(key)) return 0;
    pthread_once(&initialized,load_settings); key = black_KeyCanonical(key);
    if (key >= 0 && atomic_load(&bindings[action][1-slot]) == key) atomic_store(&bindings[action][1-slot],-1);
    return save_setting(&bindings[action][slot],key);
}
int black_SettingsResetKeyboard(void)
{
    pthread_once(&initialized,load_settings); const BlackKeyAction *actions = black_KeyActions();
    for (int i=0;i<BLACK_ACTION_COUNT;++i) for(int slot=0;slot<2;++slot) atomic_store(&bindings[i][slot],actions[i].keys[slot]);
    for (int i=0;i<4;++i) atomic_store(&mouse_speed[i],50);
    for (int i=0;i<2;++i) atomic_store(&mouse_invert[i],0);
    atomic_store(&walk_speed,50); return save_setting(&mouse_hold_aim,1);
}

int black_SettingsMod(int mod)
{ pthread_once(&initialized, load_settings); return mod >= 0 && mod < BLACK_MOD_COUNT ? atomic_load(&mods[mod]) : 0; }
int black_SettingsSetMod(int mod, int value)
{
    if (mod < 0 || mod >= BLACK_MOD_COUNT) return 0;
    int levels = black_mod_info[mod].levels;
    value = levels == 1 ? !!value : value < 0 ? 0 : value > levels ? levels : value;
    int success = save_setting(&mods[mod], value);
    fprintf(stderr, "[MODS] %s=%d\n", black_mod_info[mod].key, value);
    return success;
}
int black_SettingsResetMods(void)
{
    pthread_once(&initialized, load_settings);
    for (int i = 0; i < BLACK_MOD_COUNT; ++i) atomic_store(&mods[i], 0);
    atomic_store(&skip_missions, 0);
    atomic_store(&skip_any_film, 1);
    return save_setting(&skip_intros, 0);
}
void black_ModsSetRevision(const char *name, int supported)
{ atomic_store(&mods_revision, name); atomic_store(&mods_supported, !!supported); }
const char *black_ModsRevision(void) { return atomic_load(&mods_revision); }
/* -1 until the game has checked its executable. */
int black_ModsSupported(void) { return atomic_load(&mods_supported); }
/* Overridden by the game executable (game_mods.c); tools without game code
 * show no counter. */
__attribute__((weak)) uint32_t black_GameFrameCount(void) { return 0; }
/* game_flash.c: a mission is loaded (not the front end). */
__attribute__((weak)) int black_GameInMission(void) { return 0; }
/* game_flash.c: the pause menu page is up (seen in the last 0.3 s). */
__attribute__((weak)) int black_GamePaused(void) { return 0; }
/* xinput_device.c: a short START for the game. */
__attribute__((weak)) void black_InputPulseStart(void) {}

int black_SettingsInputDevice(void)
{ pthread_once(&initialized,load_settings); int cli = atomic_load(&input_cli); return cli >= 0 ? cli : atomic_load(&input_device); }
int black_SettingsSetInputDevice(int policy)
{
    if (policy < BLACK_INPUT_AUTO || policy > BLACK_INPUT_KEYBOARD) return 0;
    pthread_once(&initialized,load_settings); atomic_store(&input_cli,-1);
    return save_setting(&input_device,policy);
}
int black_SettingsButtonPrompts(void)
{ pthread_once(&initialized,load_settings); return atomic_load(&button_prompts); }
int black_SettingsSetButtonPrompts(int family)
{ return family >= 0 && family <= BLACK_PROMPTS_KEYBOARD ? save_setting(&button_prompts,family) : 0; }
int black_SettingsPadBinding(int target)
{ pthread_once(&initialized,load_settings); return target >= 0 && target < BLACK_PAD_CONTROLS ? atomic_load(&pad_bindings[target]) : -1; }
int black_SettingsSetPadBinding(int target,int source)
{ return target >= 0 && target < BLACK_PAD_CONTROLS && source >= -1 && source < BLACK_PAD_CONTROLS ? save_setting(&pad_bindings[target],source) : 0; }
