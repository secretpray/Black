/* Production discovery against deterministic SDL identities, not hardware. */
#include <SDL.h>
#include "input/xinput_xbox.h"
#include "input_profile.h"
#include "controller_monitor.h"
#include "port_menu.h"
#include <pthread.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct { int id, attached, fail, opens, closes, stops, left, right, accel; } Device;
static Device devices[8];
static int indices[8], count, init_fail = 1, init_calls;
static uint32_t ticks;
static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS];
static BOOL g_controller_connected[XBOX_MAX_CONTROLLERS];
static SHORT g_center[XBOX_MAX_CONTROLLERS][4];
static int g_center_done[XBOX_MAX_CONTROLLERS], black_rumble_needs_zero[XBOX_MAX_CONTROLLERS];
static int black_rumble_raw_active[XBOX_MAX_CONTROLLERS];
static pthread_mutex_t black_input_lock = PTHREAD_MUTEX_INITIALIZER;
static Device *device(SDL_GameController *p) { return (Device *)p; }
static int test_init(Uint32 flags) { (void)flags; ++init_calls; return init_fail ? -1 : 0; }
static int test_count(void) { return count; }
static SDL_bool test_is_controller(int i) { return indices[i] != 7; }
static SDL_JoystickID test_device_id(int i) { return devices[indices[i]].id; }
static SDL_GameController *test_open(int i)
{
    Device *d = &devices[indices[i]];
    if (d->fail) return NULL;
    assert(d->attached && d->opens == d->closes); /* detect duplicate handles */
    ++d->opens;
    return (SDL_GameController *)d;
}
static SDL_Joystick *test_joystick(SDL_GameController *p) { return (SDL_Joystick *)p; }
static SDL_JoystickID test_instance(SDL_Joystick *j) { return ((Device *)j)->id; }
static SDL_bool test_attached(SDL_GameController *p) { return device(p)->attached; }
static int test_rumble(SDL_GameController *p, Uint16 left, Uint16 right, Uint32 ms)
{
    assert(ms == 0 || ms == 1000);
    if (!left && !right) ++device(p)->stops;
    device(p)->left = left; device(p)->right = right;
    return device(p)->attached ? 0 : -1;
}
static void test_close(SDL_GameController *p)
{ Device *d = device(p); assert(d->stops >= 2); ++d->closes; }
static const char *test_name(SDL_GameController *p) { (void)p; return "Synthetic pad"; }
static SDL_JoystickGUID test_guid(SDL_Joystick *j) { (void)j; return (SDL_JoystickGUID){{0}}; }
static SDL_GameControllerType test_type(SDL_GameController *p) { (void)p; return SDL_CONTROLLER_TYPE_PS5; }
static Uint32 test_ticks(void) { return ticks; }
static void test_update(void) {}
static void test_flush(Uint32 first, Uint32 last) { (void)first; (void)last; }
static char *test_mapping(SDL_GameController *p) { (void)p; return NULL; }
static SDL_bool test_has_sensor(SDL_GameController *p, SDL_SensorType t) { (void)p; return t == SDL_SENSOR_ACCEL; }
static int test_set_sensor(SDL_GameController *p, SDL_SensorType t, SDL_bool on)
{ assert(t == SDL_SENSOR_ACCEL); device(p)->accel = on; return 0; }
static SDL_bool test_sensor_on(SDL_GameController *p, SDL_SensorType t) { return t == SDL_SENSOR_ACCEL && device(p)->accel; }
static int test_sensor_data(SDL_GameController *p, SDL_SensorType t, float *data, int n)
{ (void)t; for (int i = 0; i < n; ++i) data[i] = (float)ticks + i; return device(p)->accel ? 0 : -1; }
static Uint8 test_button(SDL_GameController *p, SDL_GameControllerButton b) { (void)p; (void)b; return 0; }
static Sint16 test_axis(SDL_GameController *p, SDL_GameControllerAxis a) { (void)p; (void)a; return 0; }
#define SDL_InitSubSystem test_init
#define SDL_NumJoysticks test_count
#define SDL_IsGameController test_is_controller
#define SDL_JoystickGetDeviceInstanceID test_device_id
#define SDL_GameControllerOpen test_open
#define SDL_GameControllerGetJoystick test_joystick
#define SDL_JoystickInstanceID test_instance
#define SDL_GameControllerGetAttached test_attached
#define SDL_GameControllerRumble test_rumble
#define SDL_GameControllerClose test_close
#define SDL_GameControllerName test_name
#define SDL_JoystickGetGUID test_guid
#define SDL_GameControllerGetType test_type
#define SDL_GetTicks test_ticks
#define SDL_GameControllerUpdate test_update
#define SDL_FlushEvents test_flush
#define SDL_GameControllerMapping test_mapping
#define SDL_GameControllerHasSensor test_has_sensor
#define SDL_GameControllerSetSensorEnabled test_set_sensor
#define SDL_GameControllerIsSensorEnabled test_sensor_on
#define SDL_GameControllerGetSensorData test_sensor_data
#define SDL_GameControllerGetButton test_button
#define SDL_GameControllerGetAxis test_axis
#include BLACK_CONTROLLER_BACKEND_SOURCE
#include "input_hotplug.inc"
int main(void)
{
    unsetenv("RECOMP_BACKGROUND");
    char folder[]="/tmp/black-hotplug-XXXXXX",settings[512];assert(mkdtemp(folder));
    snprintf(settings,sizeof settings,"%s/settings.ini",folder);setenv("BLACK_SETTINGS_FILE",settings,1);
    black_SettingsSetVibration(1);black_SettingsSetRumbleIntensity(100);black_InputSetKeyboardActive(0);
    input_poll_host(); assert(init_calls == 1 && !g_pads[0]);
    init_fail = 0; input_poll_host(); assert(init_calls == 2 && !g_pads[0]);
    devices[0] = (Device){.id = 10, .attached = 1, .fail = 1}; count = 1;
    ticks = 250; input_poll_host(); assert(!g_pads[0]);
    devices[0].fail = 0; ticks = 500; input_poll_host();
    assert(g_pads[0] == (SDL_GameController *)&devices[0] && g_controller_connected[0]);
    assert(!black_rumble_needs_zero[0] && g_center_done[0]);
    /* PlayStation pads stream full reports so Bluetooth does not idle. */
    assert(devices[0].accel);
    XBOX_VIBRATION effect={12000,32767},zero={0};
    assert(!xbox_InputSetState(0,&effect) && devices[0].right==32767);
    /* Replacement without a poll observing a smaller device count. */
    for (int cycle = 0; cycle < 3; ++cycle) {
        int old = cycle, next = cycle + 1;
        black_ControllerPublish("Synthetic pad", devices[old].id, (int[6]){0}, (int[6]){0}, ticks);
        black_PadCaptureBegin(); /* disconnect before capture's first poll */
        black_ControllerMeasure();
        devices[old].attached = 0;
        /* End the old envelope while offline for cycles 0/2. Cycle 1 keeps
         * it active to verify stale feedback remains blocked across open. */
        if(cycle!=1) xbox_InputSetState(0,&zero);
        devices[next] = (Device){.id = 11 + cycle, .attached = 1}; indices[0] = next;
        ++ticks; input_poll_host(); /* immediate detach; scan is throttled */
        assert(!g_pads[0] && !g_controller_connected[0] && devices[old].closes == 1);
        assert(black_PadCaptureTake() == -1);
        BlackControllerMonitor monitor = black_ControllerMonitor();
        assert(!monitor.connected && monitor.calibration == BLACK_CAL_DISCONNECTED);
        for (int a = 0; a < 6; ++a) assert(!monitor.raw[a] && !monitor.mapped[a]);
        ticks += 250; input_poll_host(); assert(g_pads[0] == (SDL_GameController *)&devices[next]);
        assert(devices[next].opens == 1 && black_rumble_needs_zero[0]==(cycle==1));
        assert(!xbox_InputSetState(0,&effect));
        assert(devices[next].right==(cycle==1?0:32767));
        if(cycle==1) {
            assert(!xbox_InputSetState(0,&zero));
            assert(!xbox_InputSetState(0,&effect) && devices[next].right==32767);
        }
    }
    /* A second pad retains its slot; an enumeration reorder cannot duplicate it. */
    devices[4] = (Device){.id = 20, .attached = 1}; indices[1] = 4; count = 2;
    ticks += 250; input_poll_host(); assert(g_pads[1] == (SDL_GameController *)&devices[4]);
    devices[3].attached = 0; indices[0] = 4; count = 1;
    ticks += 250; input_poll_host(); assert(!g_pads[0] && g_pads[1]);
    assert(devices[4].opens == 1);
    devices[5] = (Device){.id = 21, .attached = 1}; indices[1] = 5; count = 2;
    ticks += 250; input_poll_host(); assert(g_pads[0] == (SDL_GameController *)&devices[5]);
    assert(devices[4].opens == 1);
    devices[7].id = 30; indices[2] = 7; count = 3;
    ticks += 250; input_poll_host(); assert(!devices[7].opens);
    setenv("RECOMP_BACKGROUND", "1", 1); devices[5].attached = 0;
    indices[0] = 4; count = 1;
    ticks += 250; input_poll_host(); assert(g_pads[0]); /* script-only isolation */
    unsetenv("RECOMP_BACKGROUND"); input_poll_host(); assert(!g_pads[0]);
    devices[4].attached = 0; count = 0; input_poll_host(); assert(!g_pads[1]);
    unlink(settings);rmdir(folder);
    puts("Production hotplug: identity/retry/replacement/three cycles/stop/capture/slots/full reports: PASS (synthetic SDL)");
}
