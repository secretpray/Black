static void black_GameVibrationSync(void)
{
    uint32_t game = MEM32(0x2D45F0), input = MEM32(0x2D45E8);
    int enabled = black_SettingsVibration();
    /* Profile loads can replace BLACK's saved flag after boot. The port's
     * persisted setting must enable the game envelope as well as host motors. */
    if (black_flash_memory_ok(game, 0x20169, g_xbox_total_ram))
        MEM8(game + 0x20168) = enabled;
    if (black_flash_memory_ok(input, 0xACD, g_xbox_total_ram))
        MEM8(input + 0xACC) = enabled;
}

void sub_0027DDF8(void)
{
    black_GameVibrationSync();
    black_OriginalInputState();
}

static void black_VibrationSettingFromGame(void)
{
    uint32_t game = MEM32(0x2D45F0);
    if (black_flash_memory_ok(game, 0x20169, g_xbox_total_ram))
        black_SettingsSetVibration(MEM8(game + 0x20168) != 0);
}

void sub_00119C00(void)
{
    black_OriginalVibrationMenu();
    black_VibrationSettingFromGame();
}

void sub_00119DD0(void)
{
    black_OriginalVibrationPause();
    black_VibrationSettingFromGame();
}

void sub_0027DE6B(void)
{
    uint32_t handle = MEM32(g_esp + 4), feedback = MEM32(g_esp + 8);
    if (handle != 0xD0DE0000u) {
        g_eax = 1167; /* Same virtual port-0 handle as native XInputGetState. */
    } else if (!black_flash_memory_ok(feedback, 70, g_xbox_total_ram)) {
        g_eax = 87; /* ERROR_INVALID_PARAMETER */
    } else {
        /* Xbox's packed feedback header is 66 bytes. BLACK writes the right
         * motor at feedback+68 (sub_000C5F50), left at +66 (sub_000C5FB0).
         * This is not the host's four-byte XBOX_VIBRATION layout. */
        XBOX_VIBRATION vibration = {MEM16(feedback + 66), MEM16(feedback + 68)};
        DWORD result = xbox_InputSetState(0, &vibration);
        if (getenv("RECOMP_INPUT_DEBUG")) {
            static unsigned reports;
            static XBOX_VIBRATION previous;
            if ((reports == 0 || previous.wLeftMotorSpeed != vibration.wLeftMotorSpeed ||
                previous.wRightMotorSpeed != vibration.wRightMotorSpeed) && reports < 128) {
                ++reports;
                previous = vibration;
                fprintf(stderr, "[RUMBLE] XInput left=%u right=%u host=%u\n",
                    vibration.wLeftMotorSpeed, vibration.wRightMotorSpeed, (unsigned)result);
            }
        }
        /* Port 0 is a persistent virtual pad, including keyboard-only play.
         * Complete its request even if no physical motor is available. */
        MEM32(feedback) = 0;
        uint32_t event = MEM32(feedback + 4);
        if (event) black_InputFeedbackEvent(event);
        g_eax = 997; /* ERROR_IO_PENDING; header already contains completion. */
    }
    g_esp += 12; /* stdcall: return address and two arguments. */
}
