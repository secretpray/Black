# The input has all prior device/resampler fixes. Freeze it for controls
# that specifically remove voice waiting, IRQ publication or serialization.
set(BLACK_APU_LOCK_BASELINE "${CMAKE_BINARY_DIR}/runtime-overlay/apu_lock_baseline.c")
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame.inc" lock_frame)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame_previous.inc" previous_frame)
string(REPLACE "${lock_frame}" "${previous_frame}" lock_baseline "${voice}")
file(WRITE "${BLACK_APU_LOCK_BASELINE}" "${lock_baseline}")
black_voice_replace("#include \"apu_state.h\""
    "#include \"apu_state.h\"\nextern void black_apu_publish_pending_irq(MCPXAPUState *d);")
# Decoded trap registers belong to the pending event, including while the
# ISR issues ordinary VOICE_LOCK/SET_CURRENT_VOICE writes to clean it up.
black_voice_replace("    d->regs[NV_PAPU_FEDECMETH] = method;\n    d->regs[NV_PAPU_FEDECPARAM] = argument;"
    "    if ((qatomic_read(&d->regs[NV_PAPU_FECTL]) & NV_PAPU_FECTL_FEMETHMODE) !=\n        NV_PAPU_FECTL_FEMETHMODE_TRAPPED) {\n        d->regs[NV_PAPU_FEDECMETH] = method;\n        d->regs[NV_PAPU_FEDECPARAM] = argument;\n    }")
black_voice_replace("    /* Dispatch known methods through fe_method */\n    fe_method(d, (uint32_t)addr, (uint32_t)val);"
    "    /* Serialize all PIO with the frame and other guest writers. The\n     * existing shim mutex is recursive: VOICE_LOCK also acquires it. */\n    qemu_mutex_lock(&d->lock);\n    fe_method(d, (uint32_t)addr, (uint32_t)val);\n    qemu_mutex_unlock(&d->lock);")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_vp_frame_previous.inc")
