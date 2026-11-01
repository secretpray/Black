/* TGP MouseLook: native-C hook at 0x1BD6A8, before the game's pitch clamp/yaw
 * wrapping. Relative mouse motion is consumed by the game once, not by USB. */
static float black_mouse_deflection(float degrees, float full, float curve)
{
    if (!(full > 0)) return 0;
    float x = fminf(fabsf(degrees) / full, 1);
    if (curve > 0) x = powf(x, 1 / curve);
    return copysignf(x, degrees);
}
void black_MouseLook(uint32_t controller, uint32_t world, float zoom)
{
    float dx, dy;
    int aiming = MEM8(controller + 0x45) && zoom < 1;
    if (!black_MouseTake(black_InputNowMs(), aiming, &dx, &dy)) return;
    float yaw = remainderf(-dx * .05f, 360), pitch = dy * .05f;
    if (getenv("RECOMP_INPUT_DEBUG")) {
        static _Thread_local uint64_t last_log;
        uint64_t now = black_InputNowMs();
        if (now - last_log >= 1000) {
            last_log = now;
            fprintf(stderr,"[MOUSE-LOOK] dx=%.3f dy=%.3f aim=%d yaw=%.4f pitch=%.4f\n",dx,dy,aiming,yaw,pitch);
        }
    }
    MEMF(controller + 0x18) += yaw;
    MEMF(controller + 0x1C) += pitch;
    if (MEMF(controller + 0xD8) == 0 && MEMF(controller + 0xDC) == 0) {
        float frame = zoom * MEMF(world + 0x1C), curve = MEMF(controller + 0xC8);
        MEMF(controller + 0xD8) = black_mouse_deflection(yaw, frame * MEMF(controller + 0xB8), curve);
        MEMF(controller + 0xDC) = black_mouse_deflection(-pitch, frame * MEMF(controller + 0xBC), curve);
    }
}
