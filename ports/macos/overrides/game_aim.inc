/* TGP HoldAim at 0x1EC903, with native-C guest memory access.
 * ebx is the player's controller; the current weapon is esp+0x18.
 * Keyboard/mouse and controller have independent hold-to-aim preferences. */
void black_HoldAim(uint32_t controller, uint32_t weapon)
{
    int hold = black_InputKeyboardActive() ? black_SettingsMouseHoldAim() : black_SettingsHoldAim();
    if (!hold) {
        uint32_t data = MEM32(weapon + 0xEC);
        if (data && MEMF(data + 0xB4) != 1.0f) return;
    }
    /* A weapon without zoom must not leave a toggled aim for the next weapon. */
    MEM8(controller + 0x45) = MEM8(controller + 0x104);
}
