/* Local discovery overlay. SDL indices are transient; instance IDs are identity.
 * Called only while black_input_lock is held, including native menu polling. */
static void open_controllers(void)
{
    if (getenv("RECOMP_BACKGROUND")) return;
    int count = SDL_NumJoysticks();
    for (int index = 0; index < count; ++index) {
        if (!SDL_IsGameController(index)) continue;
        SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
        if (instance < 0) continue;
        int known = 0, slot = -1;
        for (int p = 0; p < XBOX_MAX_CONTROLLERS; ++p) {
            if (!g_pads[p]) { if (slot < 0) slot = p; }
            else if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[p])) == instance)
                known = 1;
        }
        if (known || slot < 0) continue;
        SDL_GameController *pad = SDL_GameControllerOpen(index);
        if (!pad) {
            fprintf(stderr, "[HOTPLUG] open failed instance=%d: %s\n", (int)instance, SDL_GetError());
            continue; /* retry at the next scan, even if the count is unchanged */
        }
        g_pads[slot] = pad;
        g_controller_connected[slot] = TRUE;
        memset(g_center[slot], 0, sizeof g_center[slot]);
        g_center_done[slot] = 1; /* host-centered axes, no automatic learning */
        /* A zero already received while disconnected ends the old envelope.
         * Requiring another zero after open would swallow the first fresh shot. */
        black_rumble_needs_zero[slot] = black_rumble_raw_active[slot];
        SDL_GameControllerRumble(pad, 0, 0, 0);
        /* Without rumble or sensors SDL leaves a PlayStation pad in its simple
         * report mode, which reports changes only. Over Bluetooth that link
         * idles: after ~5 min away the first input came 4 s late (run-swo7k1ig,
         * run-s8_idhqp); with the accelerometer on it kept streaming and
         * answered at once (run-vwh5aku1). The game reads no sensor data. */
        SDL_GameControllerType family = SDL_GameControllerGetType(pad);
        int streaming = (family == SDL_CONTROLLER_TYPE_PS4 || family == SDL_CONTROLLER_TYPE_PS5) &&
            SDL_GameControllerHasSensor(pad, SDL_SENSOR_ACCEL) &&
            SDL_GameControllerSetSensorEnabled(pad, SDL_SENSOR_ACCEL, SDL_TRUE) == 0;
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(pad)), guid, sizeof guid);
        fprintf(stderr, "[HOTPLUG] connected port=%d instance=%d name=\"%s\" guid=%s type=%d hid_ps5=%s\n",
            slot, (int)instance, SDL_GameControllerName(pad), guid,
            (int)SDL_GameControllerGetType(pad), SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5));
        if (streaming) fprintf(stderr, "[HOTPLUG] port=%d full reports on (accelerometer)\n", slot);
        if (getenv("RECOMP_INPUT_DEBUG")) {
            char *mapping = SDL_GameControllerMapping(pad);
            fprintf(stderr, "[HOTPLUG] mapping=%s\n", mapping ? mapping : "(none)");
            SDL_free(mapping);
        }
    }
}

static void input_rescan(void)
{
    for (int p = 0; p < XBOX_MAX_CONTROLLERS; ++p) {
        if (!g_pads[p] || SDL_GameControllerGetAttached(g_pads[p])) continue;
        int instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[p]));
        /* Best effort: an already disconnected transport may reject this.
         * Nonzero effects also have a finite SDL duration, never infinite. */
        int stopped = SDL_GameControllerRumble(g_pads[p], 0, 0, 0);
        fprintf(stderr, "[HOTPLUG] disconnected port=%d instance=%d stop=%d\n", p, instance, stopped);
        SDL_GameControllerClose(g_pads[p]);
        g_pads[p] = NULL;
        g_controller_connected[p] = FALSE;
        black_rumble_needs_zero[p] = 1;
        if (!p) {
            black_PadCapturePoll(0, -1, 0);
            black_ControllerPublish(NULL, -1, NULL, NULL, SDL_GetTicks());
        }
    }
}

static void input_poll_host(void)
{
    static int ready, scanned;
    static uint32_t last_scan;
    if (getenv("RECOMP_BACKGROUND")) return;
    if (!ready) {
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            fprintf(stderr, "[HOTPLUG] SDL init failed: %s (will retry)\n", SDL_GetError());
            return;
        }
        SDL_version linked;
        SDL_GetVersion(&linked);
        fprintf(stderr, "[HOTPLUG] SDL=%u.%u.%u platform=%s\n", linked.major, linked.minor, linked.patch, SDL_GetPlatform());
        ready = 1;
    }
    SDL_GameControllerUpdate();
    /* This SDL instance owns input only; Cocoa owns application events. */
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    input_rescan(); /* detach must clear state on this poll, not in 500 ms */
    uint32_t now = SDL_GetTicks();
    {
        /* Pad 0 around focus changes: whether its state still moves in the
         * background, and how long after a return the first change arrives.
         * A few lines per switch; the pad's handle and SDL's count go too. */
        static int focused = 1, tracing;
        static uint32_t left_at, back_at, last_line, changes, first_change;
        static uint32_t previous_state;
        SDL_GameController *pad = g_pads[0];
        uint32_t state = 0;
        if (pad && SDL_GameControllerGetAttached(pad)) {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && b < 24; ++b)
                if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b)) state |= 1u << b;
            for (int a = 0; a < 6; ++a)
                state = state * 31u + (uint32_t)(SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)a) >> 12);
        }
        int instance = pad ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) : -1;
        if (state != previous_state) { previous_state = state; ++changes; if (tracing && !first_change) first_change = now - back_at; }
        /* Accelerometer noise differs in every report, so where open_controllers
         * turned it on it shows whether reports still arrive untouched. */
        static float previous_accel[3];
        static uint32_t reports, first_report, last_report_at;
        if (pad && SDL_GameControllerGetAttached(pad) && SDL_GameControllerIsSensorEnabled(pad, SDL_SENSOR_ACCEL)) {
            float accel[3];
            if (SDL_GameControllerGetSensorData(pad, SDL_SENSOR_ACCEL, accel, 3) == 0 &&
                memcmp(accel, previous_accel, sizeof accel)) {
                memcpy(previous_accel, accel, sizeof accel);
                ++reports;
                if (tracing && !first_report) first_report = now - back_at;
                if (last_report_at && now - last_report_at >= 2000)
                    fprintf(stderr, "[FOCUS-PAD] reports resumed after a %u ms gap (focused=%d)\n",
                        now - last_report_at, black_InputFocused());
                last_report_at = now;
            }
        }
        int active = black_InputFocused();
        if (active != focused) {
            focused = active;
            if (!active) {
                left_at = now; changes = 0; reports = 0; last_line = now; tracing = 0;
                fprintf(stderr, "[FOCUS-PAD] background instance=%d attached=%d joysticks=%d\n",
                    instance, pad ? SDL_GameControllerGetAttached(pad) : 0, SDL_NumJoysticks());
            } else {
                back_at = now; tracing = 1; first_change = 0; first_report = 0; last_line = now;
                fprintf(stderr, "[FOCUS-PAD] foreground after %u s, %u background changes, %u reports, last report %u ms ago, instance=%d attached=%d joysticks=%d\n",
                    (now - left_at) / 1000, changes, reports, last_report_at ? now - last_report_at : 0,
                    instance, pad ? SDL_GameControllerGetAttached(pad) : 0, SDL_NumJoysticks());
                changes = 0; reports = 0;
            }
        } else if (!active && now - last_line >= 30000) {
            last_line = now;
            fprintf(stderr, "[FOCUS-PAD] background %u s: changes=%u reports=%u instance=%d attached=%d joysticks=%d\n",
                (now - left_at) / 1000, changes, reports, instance, pad ? SDL_GameControllerGetAttached(pad) : 0, SDL_NumJoysticks());
        } else if (tracing && (now - last_line >= 1000 || (first_change && now - back_at >= 30000))) {
            last_line = now;
            fprintf(stderr, "[FOCUS-PAD] +%u ms: first change %s%u ms, first report %u ms, changes=%u reports=%u instance=%d attached=%d joysticks=%d\n",
                now - back_at, first_change ? "at " : "none yet ", first_change ? first_change : now - back_at,
                first_report, changes, reports, instance, pad ? SDL_GameControllerGetAttached(pad) : 0, SDL_NumJoysticks());
            if (now - back_at >= 30000) tracing = 0;
        }
    }
    if (!scanned || (uint32_t)(now - last_scan) >= 250) {
        scanned = 1;
        last_scan = now;
        open_controllers(); /* reconcile identities even when count is equal */
    }
}
