# Native arm64 path. TGP's Windows host and this runtime have different ABIs;
# their generated sources and game hooks must never be linked together.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(BLACK_BUILD_GAME "Build BLACK from locally generated game code" ON)
set(BLACK_MAC_GAME "${CMAKE_SOURCE_DIR}/ports/macos/game")
set(BLACK_MAC_RUNTIME "${CMAKE_SOURCE_DIR}/ports/macos/runtime")
set(BLACK_GEN_DIR "${CMAKE_SOURCE_DIR}/out/macos/generated" CACHE PATH "Local generated C")

foreach(source IN ITEMS "${BLACK_MAC_GAME}" "${BLACK_MAC_RUNTIME}")
    if(NOT EXISTS "${source}/CMakeLists.txt")
        message(FATAL_ERROR "Missing macOS sources: run git submodule update --init ports/macos/game ports/macos/runtime")
    endif()
endforeach()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR
   (CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64"))
    message(FATAL_ERROR "The macOS port currently targets Apple Silicon arm64 only")
endif()

add_subdirectory("${BLACK_MAC_RUNTIME}" "${CMAKE_BINARY_DIR}/runtime")
# SMAA for the presenter: TGP's reference shader and lookups (src/d3d, MIT)
# reach C through a C++17 shim; the Metal passes live beside the presenter.
enable_language(CXX)
add_library(black-smaa STATIC
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/smaa_data.cpp"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/smaa_metal.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/upscale_metal.m")
set_target_properties(black-smaa PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(black-smaa PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(black-smaa PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>)
target_link_libraries(black-smaa PUBLIC "-framework Metal" "-framework MetalFX" "-framework Foundation")
add_library(black-port-ui STATIC
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/flash_input.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/flash_menu.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_prompts.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/hud_prompts.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/keyboard_mouse.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/input_profile.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/controller_look.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/controller_monitor.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/frame_trace.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/frame_wait.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/display_gamma.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/port_settings.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/app_install.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/app_bootstrap.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/app_setup.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_translation.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/input_fonts.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/input_glyphs.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/subtitles.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/black_ui.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/save_profiles.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/slot_info.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/window_state.m"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/black_profile.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_checkpoint.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/port_menu.m")
target_link_libraries(black-port-ui PUBLIC "-framework Cocoa" "-framework QuartzCore" "-framework GameController" "-framework CoreText" "-framework ImageIO")
target_link_libraries(xbox_input PUBLIC black-port-ui)
target_link_libraries(xbox_video PUBLIC black-port-ui black-smaa)
# HD texture replacement: map, pack index, DDS/PNG and loader threads; the
# renderer side is texture_pack_gl.inc in the texture.c overlay.
add_library(black-texture-pack STATIC "${CMAKE_SOURCE_DIR}/ports/macos/overrides/texture_pack.c" "${CMAKE_SOURCE_DIR}/ports/macos/overrides/texture_dump.c")
target_compile_options(black-texture-pack PRIVATE -Wall -Wextra)
target_link_libraries(black-texture-pack PUBLIC black-port-ui
    "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")
# The settings menu checks and links a pack before turning replacement on.
target_link_libraries(black-port-ui PUBLIC black-texture-pack)
include("${CMAKE_SOURCE_DIR}/cmake/BlackRuntimeOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackAPUOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackInputOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackInterruptOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackVoiceOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackGraphicsTrace.cmake")
get_target_property(BLACK_VIDEO_SOURCES xbox_video SOURCES)
list(REMOVE_ITEM BLACK_VIDEO_SOURCES fb_present_posix.m)
set_property(TARGET xbox_video PROPERTY SOURCES ${BLACK_VIDEO_SOURCES}
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/fb_present_posix.m")
get_target_property(BLACK_KERNEL_SOURCES xbox_kernel SOURCES)
list(REMOVE_ITEM BLACK_KERNEL_SOURCES kernel_sync.c kernel_bridge.c kernel_hal.c)
set_property(TARGET xbox_kernel PROPERTY SOURCES ${BLACK_KERNEL_SOURCES}
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/kernel_sync.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/kernel_bridge.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/kernel_hal.c")
set_property(TARGET xbox_input PROPERTY SOURCES
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/keyboard_input.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/xinput_device.c")
set_property(TARGET xbox_apu PROPERTY SOURCES
    "${BLACK_APU_CORE_OVERLAY}"
    "${BLACK_APU_VOICE_OVERLAY}"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_dsp.c"
    "${BLACK_MAC_RUNTIME}/src/apu/apu_mmio_hook.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_xaudio2.c")
include("${CMAKE_SOURCE_DIR}/cmake/BlackRealDSP.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackFileOverlay.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackNv2aInterrupt.cmake")

# Compile every production runtime library and the extracted NV2A renderer.
add_custom_target(black-runtime-check ALL DEPENDS
    platform xbox_kernel xbox_d3d8 xbox_dsound xbox_input xbox_usb
    xbox_apu xbox_nv2a xbox_video xemu_nv2a)

enable_testing()
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLACK_GLIB REQUIRED IMPORTED_TARGET glib-2.0)
add_executable(black-psh-regression tools/macos/psh_regression_test.c
    "${black_psh_overlay}" "${trace_root}/hw/xbox/nv2a/pgraph/glsl/common.c")
target_include_directories(black-psh-regression PRIVATE $<TARGET_PROPERTY:xemu_nv2a,INCLUDE_DIRECTORIES>)
target_compile_options(black-psh-regression PRIVATE -ffunction-sections)
target_link_options(black-psh-regression PRIVATE -Wl,-dead_strip)
target_link_libraries(black-psh-regression PRIVATE PkgConfig::BLACK_GLIB)
add_test(NAME macos_psh_regression COMMAND black-psh-regression)
find_package(Threads REQUIRED)
include("${CMAKE_SOURCE_DIR}/cmake/BlackInterruptTests.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/BlackVoiceLockTests.cmake")
add_executable(black-menu-input tools/macos/menu_input_test.c)
add_test(NAME macos_menu_input COMMAND black-menu-input)
add_executable(black-text-pad tools/macos/text_pad_test.c)
add_test(NAME macos_text_pad COMMAND black-text-pad)
add_executable(black-menu-repeat tools/macos/menu_repeat_test.c)
add_test(NAME macos_menu_repeat COMMAND black-menu-repeat)
add_executable(black-window-geometry tools/macos/window_geometry_test.c)
add_test(NAME macos_window_geometry COMMAND black-window-geometry)
add_executable(black-save-profiles tools/macos/save_profiles_test.m
    ports/macos/overrides/save_profiles.m ports/macos/overrides/black_profile.c)
target_link_libraries(black-save-profiles PRIVATE "-framework Foundation")
add_test(NAME macos_save_profiles COMMAND black-save-profiles)
add_executable(black-slot-info tools/macos/slot_info_test.m ports/macos/overrides/slot_info.m
    ports/macos/overrides/black_profile.c ports/macos/overrides/display_gamma.c)
target_link_libraries(black-slot-info PRIVATE "-framework Cocoa" "-framework ImageIO")
add_test(NAME macos_slot_info COMMAND black-slot-info)
add_executable(black-port-settings tools/macos/port_settings_test.c
    ports/macos/overrides/port_settings.c ports/macos/overrides/keyboard_mouse.c
    ports/macos/overrides/game_checkpoint.c
    ports/macos/overrides/flash_input.c ports/macos/overrides/input_profile.c)
target_link_libraries(black-port-settings PRIVATE Threads::Threads)
add_test(NAME macos_port_settings COMMAND black-port-settings)
add_executable(black-checkpoints tools/macos/checkpoint_test.c)
target_link_libraries(black-checkpoints PRIVATE Threads::Threads)
add_test(NAME macos_checkpoints COMMAND black-checkpoints)
add_executable(black-menu-layout tools/macos/menu_layout_test.c)
add_test(NAME macos_menu_layout COMMAND black-menu-layout)
add_executable(black-hud-prompts tools/macos/hud_prompts_test.c)
target_link_libraries(black-hud-prompts PRIVATE black-port-ui)
add_test(NAME macos_hud_prompts COMMAND black-hud-prompts)
add_executable(black-game-prompts tools/macos/game_prompts_test.c)
target_link_libraries(black-game-prompts PRIVATE black-port-ui)
add_test(NAME macos_game_prompts COMMAND black-game-prompts)
add_executable(black-input-profile tools/macos/input_profile_test.c)
target_include_directories(black-input-profile PRIVATE "${BLACK_MAC_RUNTIME}/src")
target_link_libraries(black-input-profile PRIVATE black-port-ui)
add_test(NAME macos_input_profile COMMAND black-input-profile)
add_executable(black-menu-preview tools/macos/menu_preview.m)
add_executable(black-app-install tools/macos/app_install_test.m ports/macos/overrides/app_install.m)
target_link_libraries(black-app-install PRIVATE "-framework Foundation")
add_test(NAME macos_app_install COMMAND black-app-install)
add_executable(black-translation tools/macos/translation_test.m)
target_link_libraries(black-translation PRIVATE black-port-ui)
add_test(NAME macos_translation COMMAND black-translation)
target_link_libraries(black-menu-preview PRIVATE black-port-ui)
# Needs your own disc folder or image, so it is run by hand, not by CTest.
add_executable(black-setup-preview tools/macos/setup_preview.m)
target_link_libraries(black-setup-preview PRIVATE black-port-ui)
add_executable(black-controller-axes tools/macos/controller_axes_test.c)
add_test(NAME macos_controller_axes COMMAND black-controller-axes)
add_executable(black-controller-monitor tools/macos/controller_monitor_test.c
    ports/macos/overrides/controller_monitor.c)
target_link_libraries(black-controller-monitor PRIVATE Threads::Threads)
add_test(NAME macos_controller_monitor COMMAND black-controller-monitor)
add_executable(black-controller-look tools/macos/controller_look_test.c)
target_link_libraries(black-controller-look PRIVATE black-port-ui)
add_test(NAME macos_controller_look COMMAND black-controller-look)
add_executable(black-display-gamma tools/macos/display_gamma_test.c)
target_link_libraries(black-display-gamma PRIVATE black-port-ui)
add_test(NAME macos_display_gamma COMMAND black-display-gamma)
add_executable(black-controller-aim tools/macos/controller_aim_test.c)
target_link_libraries(black-controller-aim PRIVATE black-port-ui)
add_test(NAME macos_controller_aim COMMAND black-controller-aim)
add_executable(black-keyboard-mouse tools/macos/keyboard_mouse_test.c)
target_link_libraries(black-keyboard-mouse PRIVATE black-port-ui)
add_test(NAME macos_keyboard_mouse COMMAND black-keyboard-mouse)
add_executable(black-game-mouse tools/macos/game_mouse_test.c)
target_link_libraries(black-game-mouse PRIVATE black-port-ui)
add_test(NAME macos_game_mouse COMMAND black-game-mouse)
add_executable(black-keyboard-host tools/macos/keyboard_host_test.m)
target_link_libraries(black-keyboard-host PRIVATE black-port-ui)
add_test(NAME macos_keyboard_host COMMAND black-keyboard-host)
add_executable(black-flash-menu tools/macos/flash_menu_test.c)
target_link_libraries(black-flash-menu PRIVATE black-port-ui)
add_test(NAME macos_flash_menu COMMAND black-flash-menu)
add_executable(black-flash-input tools/macos/flash_input_test.c)
target_link_libraries(black-flash-input PRIVATE black-port-ui)
add_test(NAME macos_flash_input COMMAND black-flash-input)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_flash.c" game_flash_source)
black_apu_extract_function(game_flash_source "void sub_00113BF0(void)\n{" hint_wrapper)
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/hint_wrapper_test.inc" "${hint_wrapper}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_flash.c")
add_executable(black-flash-call tools/macos/flash_call_test.c)
target_compile_definitions(black-flash-call PRIVATE BLACK_HINT_WRAPPER_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/hint_wrapper_test.inc")
add_test(NAME macos_flash_call COMMAND black-flash-call)
add_executable(black-flash-menu-asan tools/macos/flash_menu_test.c
    ports/macos/overrides/flash_menu.c ports/macos/overrides/flash_input.c
    ports/macos/overrides/keyboard_mouse.c ports/macos/overrides/port_settings.c ports/macos/overrides/input_profile.c
    ports/macos/overrides/game_checkpoint.c)
target_compile_options(black-flash-menu-asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(black-flash-menu-asan PRIVATE -fsanitize=address)
target_link_libraries(black-flash-menu-asan PRIVATE Threads::Threads)
add_test(NAME macos_flash_menu_asan COMMAND black-flash-menu-asan)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/xinput_device.c" input_source)
black_apu_extract_function(input_source "static int black_menu_input(DWORD port, XBOX_INPUT_STATE *state)\n{" input_navigation)
black_apu_extract_function(input_source "static DWORD black_InputGetState(DWORD port, XBOX_INPUT_STATE *state)\n{" input_routing)
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/input_routing_test.inc" "${input_navigation}\n${input_routing}\n")
add_executable(black-input-routing tools/macos/input_routing_test.c)
target_include_directories(black-input-routing PRIVATE "${BLACK_MAC_RUNTIME}/src" "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
target_compile_definitions(black-input-routing PRIVATE BLACK_INPUT_ROUTING_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/input_routing_test.inc")
target_link_libraries(black-input-routing PRIVATE black-port-ui SDL2::SDL2 Threads::Threads)
add_test(NAME macos_input_routing COMMAND black-input-routing)
black_apu_extract_function(input_source "static SDL_bool black_InputSetHint(const char *name, const char *value)\n{" input_hint)
black_apu_extract_function(input_source "DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *vibration)\n{" input_rumble)
black_apu_extract_function(input_source "static DWORD black_SDLInputSetState(DWORD port, const XBOX_VIBRATION *vibration)\n{" input_host_rumble)
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/controller_backend_test.inc" "${input_hint}\n${input_host_rumble}\n${input_rumble}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/xinput_device.c")
add_executable(black-controller-backend tools/macos/controller_backend_test.c)
find_package(SDL2 REQUIRED)
target_include_directories(black-controller-backend PRIVATE "${BLACK_MAC_RUNTIME}/src"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
target_compile_definitions(black-controller-backend PRIVATE
    BLACK_CONTROLLER_BACKEND_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/controller_backend_test.inc")
target_link_libraries(black-controller-backend PRIVATE black-port-ui SDL2::SDL2 Threads::Threads)
add_test(NAME macos_controller_backend COMMAND black-controller-backend)
add_executable(black-input-hotplug tools/macos/input_hotplug_test.c)
target_include_directories(black-input-hotplug PRIVATE "${BLACK_MAC_RUNTIME}/src"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
target_compile_definitions(black-input-hotplug PRIVATE
    BLACK_CONTROLLER_BACKEND_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/controller_backend_test.inc")
target_link_libraries(black-input-hotplug PRIVATE black-port-ui SDL2::SDL2 Threads::Threads)
add_test(NAME macos_input_hotplug COMMAND black-input-hotplug)
set_tests_properties(macos_input_hotplug PROPERTIES TIMEOUT 5)
include("${CMAKE_SOURCE_DIR}/cmake/BlackRumbleTests.cmake")
add_executable(black-input-owner tools/macos/input_owner_test.c)
add_test(NAME macos_input_owner COMMAND black-input-owner)
add_executable(black-game-clock tools/macos/game_clock_test.c)
add_test(NAME macos_game_clock COMMAND black-game-clock)
add_executable(black-audio-routing tools/macos/audio_routing_test.c)
add_test(NAME macos_audio_routing COMMAND black-audio-routing)
add_executable(black-audio-pacing tools/macos/audio_pacing_test.c)
add_test(NAME macos_audio_pacing COMMAND black-audio-pacing)
add_executable(black-audio-limiter tools/macos/audio_limiter_test.c)
add_test(NAME macos_audio_limiter COMMAND black-audio-limiter)
add_executable(black-dsp-headroom tools/macos/dsp_headroom_test.c)
add_test(NAME macos_dsp_headroom COMMAND black-dsp-headroom)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_dsp.c" dsp_adapter)
set(dsp_slice_init "        memset(black_real_dsp_output + off, 0,\n               NUM_SAMPLES_PER_FRAME * sizeof black_real_dsp_output[0]);\n")
string(FIND "${dsp_adapter}" "${dsp_slice_init}" dsp_slice_anchor)
if(dsp_slice_anchor EQUAL -1)
    message(FATAL_ERROR "DSP slice negative-control anchor changed")
endif()
string(REPLACE "${dsp_slice_init}" "" dsp_baseline "${dsp_adapter}")
string(REPLACE "        d->monitor.dsp_wrote_frame = true;\n" "" dsp_baseline "${dsp_baseline}")
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/dsp_slice_baseline.c" "${dsp_baseline}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_dsp.c")
foreach(variant IN ITEMS fixed baseline)
    add_executable(black-dsp-slice-${variant} tools/macos/dsp_slice_test.c)
    target_include_directories(black-dsp-slice-${variant} PRIVATE
        "${BLACK_MAC_RUNTIME}/src" "${BLACK_MAC_RUNTIME}/src/apu"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
    if(variant STREQUAL "fixed")
        set(dsp_source "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_dsp.c")
    else()
        set(dsp_source "${CMAKE_BINARY_DIR}/runtime-overlay/dsp_slice_baseline.c")
    endif()
    target_compile_definitions(black-dsp-slice-${variant} PRIVATE
        BLACK_HAVE_REAL_DSP=1 BLACK_DSP_SOURCE="${dsp_source}")
    add_test(NAME macos_dsp_slice_${variant} COMMAND black-dsp-slice-${variant})
    set_tests_properties(macos_dsp_slice_${variant} PROPERTIES TIMEOUT 5)
    if(variant STREQUAL "baseline")
        set_tests_properties(macos_dsp_slice_${variant} PROPERTIES WILL_FAIL TRUE)
    endif()
    add_executable(black-apu-device-${variant} tools/macos/apu_device_test.c)
    target_include_directories(black-apu-device-${variant} PRIVATE
        "${BLACK_MAC_RUNTIME}/src/apu" "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
    target_link_libraries(black-apu-device-${variant} PRIVATE platform)
    # Catch the old raw/decoded stack overwrite deterministically; a signal
    # alone would not qualify as a successful negative control in CTest.
    target_compile_options(black-apu-device-${variant} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(black-apu-device-${variant} PRIVATE -fsanitize=address)
    if(variant STREQUAL "fixed")
        set(vp_source "${BLACK_APU_VOICE_OVERLAY}")
    else()
        set(vp_source "${BLACK_MAC_RUNTIME}/src/apu/apu_vp.c")
    endif()
    target_compile_definitions(black-apu-device-${variant} PRIVATE
        BLACK_VP_SOURCE="${vp_source}"
        BLACK_IRQ_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/apu_irq_${variant}.inc")
    if(variant STREQUAL "fixed")
        target_compile_definitions(black-apu-device-${variant} PRIVATE BLACK_TEST_RESAMPLE_FIXED=1)
    endif()
    add_executable(black-voice-resample-${variant} tools/macos/voice_resample_test.c)
    target_include_directories(black-voice-resample-${variant} PRIVATE
        "${BLACK_MAC_RUNTIME}/src/apu" "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
    target_link_libraries(black-voice-resample-${variant} PRIVATE platform)
    target_compile_options(black-voice-resample-${variant} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(black-voice-resample-${variant} PRIVATE -fsanitize=address)
    if(variant STREQUAL "fixed")
        set(resample_source "${BLACK_APU_VOICE_OVERLAY}")
    else()
        set(resample_source "${BLACK_APU_RESAMPLE_BASELINE}")
    endif()
    target_compile_definitions(black-voice-resample-${variant} PRIVATE
        BLACK_VP_SOURCE="${resample_source}"
        BLACK_IRQ_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/apu_irq_fixed.inc")
    foreach(case IN ITEMS rates lifecycle streams blocks frames descriptor)
        add_test(NAME macos_resample_${case}_${variant} COMMAND black-voice-resample-${variant} ${case})
        set_tests_properties(macos_resample_${case}_${variant} PROPERTIES TIMEOUT 10
            ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:exitcode=1:detect_leaks=0")
        if(variant STREQUAL "baseline")
            set_tests_properties(macos_resample_${case}_${variant} PROPERTIES WILL_FAIL TRUE)
        endif()
    endforeach()
    foreach(case IN ITEMS latch trap ebo adpcm)
        add_test(NAME macos_apu_${case}_${variant} COMMAND black-apu-device-${variant} ${case})
        set_tests_properties(macos_apu_${case}_${variant} PROPERTIES TIMEOUT 10
            ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:exitcode=1:detect_leaks=0")
        if(variant STREQUAL "baseline")
            set_tests_properties(macos_apu_${case}_${variant} PROPERTIES WILL_FAIL TRUE)
        endif()
    endforeach()
endforeach()
file(READ "${BLACK_MAC_RUNTIME}/src/kernel/kernel_hal.c" hal_source)
string(FIND "${hal_source}" "/* ============================================================================\n * KeTickCount" hal_end)
if(hal_end EQUAL -1)
    message(FATAL_ERROR "Pinned HAL IRQL section changed")
endif()
string(SUBSTRING "${hal_source}" 0 ${hal_end} irql_source)
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/irql_test.inc" "${irql_source}")
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/audio_completion.inc" completion)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/kernel/kernel_hal.c"
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/audio_completion.inc")
foreach(variant IN ITEMS fixed baseline membership_baseline)
    set(completion_test "${completion}")
    if(variant STREQUAL "baseline")
        string(REPLACE "    if (!black_IrqlTryEnterDispatch()) {\n        g_eax = MEM32(g_esp + 4);\n        g_esp += 8;\n        return;\n    }\n" "" completion_test "${completion_test}")
        string(REPLACE "    black_IrqlLeaveDispatch();\n" "" completion_test "${completion_test}")
    elseif(variant STREQUAL "membership_baseline")
        string(REPLACE "if (MEM32(node) == node && MEM32(node + 4) == node)" "if (1)" completion_test "${completion_test}")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/completion_${variant}.inc" "${completion_test}")
    add_executable(black-audio-completion-${variant} tools/macos/audio_completion_test.c)
    target_include_directories(black-audio-completion-${variant} PRIVATE
        "${BLACK_MAC_RUNTIME}/src/kernel" "${BLACK_MAC_RUNTIME}/src")
    target_compile_definitions(black-audio-completion-${variant} PRIVATE
        BLACK_IRQL_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/irql_test.inc"
        BLACK_COMPLETION_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/completion_${variant}.inc")
    target_link_libraries(black-audio-completion-${variant} PRIVATE Threads::Threads)
    add_test(NAME macos_audio_completion_${variant} COMMAND black-audio-completion-${variant})
    set_tests_properties(macos_audio_completion_${variant} PROPERTIES TIMEOUT 5)
    if(NOT variant STREQUAL "fixed")
        set_tests_properties(macos_audio_completion_${variant} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()
foreach(variant IN ITEMS fixed baseline)
    add_executable(black-audio-irq-${variant} tools/macos/audio_irq_test.c
        "${BLACK_MAC_RUNTIME}/src/kernel/xbox_dirty_tracking.c")
    target_include_directories(black-audio-irq-${variant} PRIVATE "${BLACK_MAC_RUNTIME}/src/platform")
    target_compile_definitions(black-audio-irq-${variant} PRIVATE
        BLACK_AUDIO_IRQ_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/apu_irq_${variant}.inc")
    target_link_libraries(black-audio-irq-${variant} PRIVATE platform)
    add_test(NAME macos_audio_irq_${variant} COMMAND black-audio-irq-${variant})
    set_tests_properties(macos_audio_irq_${variant} PROPERTIES TIMEOUT 5)
    if(variant STREQUAL "baseline")
        set_tests_properties(macos_audio_irq_${variant} PROPERTIES WILL_FAIL TRUE)
    endif()
    add_executable(black-apu-epoch-${variant} tools/macos/apu_epoch_test.c)
    target_link_libraries(black-apu-epoch-${variant} PRIVATE Threads::Threads)
    if(variant STREQUAL "fixed")
        target_compile_definitions(black-apu-epoch-${variant} PRIVATE
            BLACK_EPOCH_LOOP="${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_frame_thread.inc")
    else()
        target_compile_definitions(black-apu-epoch-${variant} PRIVATE
            BLACK_EPOCH_LOOP="${CMAKE_BINARY_DIR}/runtime-overlay/apu_frame_thread_baseline.inc")
    endif()
    add_test(NAME macos_apu_epoch_${variant} COMMAND black-apu-epoch-${variant})
    set_tests_properties(macos_apu_epoch_${variant} PROPERTIES TIMEOUT 5)
    if(variant STREQUAL "baseline")
        set_tests_properties(macos_apu_epoch_${variant} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()
add_executable(black-mmio-copy tools/macos/mmio_copy_test.c)
target_include_directories(black-mmio-copy PRIVATE "${BLACK_MAC_RUNTIME}/src/kernel")
add_test(NAME macos_mmio_copy COMMAND black-mmio-copy)
add_executable(black-voice-gain tools/macos/voice_gain_test.c)
add_test(NAME macos_voice_gain COMMAND black-voice-gain)
add_executable(black-film-policy tools/macos/film_policy_test.c)
add_test(NAME macos_film_policy COMMAND black-film-policy)
add_executable(black-game-mods tools/macos/game_mods_test.c)
add_test(NAME macos_game_mods COMMAND black-game-mods)
add_executable(black-audio-fade tools/macos/audio_fade_test.c)
add_test(NAME macos_audio_fade COMMAND black-audio-fade)
file(READ "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_xaudio2.c" audio_menu_source)
black_apu_extract_function(audio_menu_source "void black_AudioSetMenuOpen(int open)\n{" audio_menu_gate)
black_apu_extract_function(audio_menu_source "int xa2_submit_samples(const int16_t *samples, int frames)\n{" audio_menu_submit)
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/audio_menu_test.inc" "${audio_menu_gate}\n${audio_menu_submit}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/apu_xaudio2.c")
add_executable(black-audio-menu tools/macos/audio_menu_test.c)
target_include_directories(black-audio-menu PRIVATE "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
target_compile_definitions(black-audio-menu PRIVATE BLACK_AUDIO_MENU_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/audio_menu_test.inc")
target_compile_options(black-audio-menu PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(black-audio-menu PRIVATE -fsanitize=address)
target_link_libraries(black-audio-menu PRIVATE SDL2::SDL2 Threads::Threads)
add_test(NAME macos_audio_menu COMMAND black-audio-menu)
add_executable(black-video-flags tools/macos/video_flags_test.c)
add_test(NAME macos_video_flags COMMAND black-video-flags)
add_executable(black-hud-layout tools/macos/hud_layout_test.c)
add_test(NAME macos_hud_layout COMMAND black-hud-layout)
add_executable(black-smaa-test tools/macos/smaa_test.m)
target_compile_options(black-smaa-test PRIVATE -fno-objc-arc)
target_link_libraries(black-smaa-test PRIVATE black-smaa)
add_test(NAME macos_smaa COMMAND black-smaa-test)
set_tests_properties(macos_smaa PROPERTIES SKIP_RETURN_CODE 77)
add_executable(black-upscale-test tools/macos/upscale_test.m)
target_compile_options(black-upscale-test PRIVATE -fno-objc-arc)
target_link_libraries(black-upscale-test PRIVATE black-smaa)
add_test(NAME macos_metalfx COMMAND black-upscale-test)
set_tests_properties(macos_metalfx PROPERTIES SKIP_RETURN_CODE 77)
add_executable(black-texture-filter tools/macos/texture_filter_test.c)
add_test(NAME macos_texture_filter COMMAND black-texture-filter)
add_executable(black-texture-pack-test tools/macos/texture_pack_test.c)
target_link_libraries(black-texture-pack-test PRIVATE black-texture-pack)
target_compile_definitions(black-texture-pack-test PRIVATE
    BLACK_TEXTURE_MAP_FILE="${CMAKE_SOURCE_DIR}/resources/textures/black-hd-reimagined.map")
add_test(NAME macos_texture_pack COMMAND black-texture-pack-test)
add_executable(black-texture-dump-test tools/macos/texture_dump_test.c)
target_link_libraries(black-texture-dump-test PRIVATE black-texture-pack)
add_test(NAME macos_texture_dump COMMAND black-texture-dump-test)
# The upstream test links platform alone, but ReadFile now calls the dirty
# tracker. Link the real implementation, rather than replacing it with a stub.
add_executable(black-guest-layout
    "${BLACK_MAC_RUNTIME}/tests/guest_layout/test_guest_layout.c"
    "${BLACK_MAC_RUNTIME}/src/kernel/xbox_dirty_tracking.c")
target_link_libraries(black-guest-layout PRIVATE platform)
add_test(NAME macos_guest_layout COMMAND black-guest-layout)
set_tests_properties(macos_guest_layout PROPERTIES TIMEOUT 20)
foreach(variant IN ITEMS fixed baseline)
    add_executable(black-notification-${variant} tools/macos/notification_event_test.c
        "${BLACK_MAC_RUNTIME}/src/kernel/xbox_dirty_tracking.c")
    target_include_directories(black-notification-${variant} PRIVATE
        "${BLACK_MAC_RUNTIME}/src" "${BLACK_MAC_RUNTIME}/src/platform")
    target_link_libraries(black-notification-${variant} PRIVATE Threads::Threads)
    if(variant STREQUAL "baseline")
        target_compile_definitions(black-notification-${variant} PRIVATE BLACK_TEST_OLD_RACE=1
            BLACK_EVENT_SOURCE="${BLACK_MAC_RUNTIME}/src/platform/win32_compat.c")
    else()
        target_compile_definitions(black-notification-${variant} PRIVATE
            BLACK_EVENT_SOURCE="${BLACK_COMPAT_OVERLAY}")
    endif()
    add_test(NAME macos_notification_${variant} COMMAND black-notification-${variant})
    set_tests_properties(macos_notification_${variant} PROPERTIES TIMEOUT 10)
    add_executable(black-sync-${variant} tools/macos/sync_event_test.c
        "${BLACK_MAC_RUNTIME}/src/kernel/xbox_dirty_tracking.c")
    target_include_directories(black-sync-${variant} PRIVATE "${BLACK_MAC_RUNTIME}/src/kernel")
    target_link_libraries(black-sync-${variant} PRIVATE platform)
    if(variant STREQUAL "baseline")
        target_compile_definitions(black-sync-${variant} PRIVATE BLACK_TEST_OLD_RACE=1)
    endif()
    add_test(NAME macos_sync_${variant} COMMAND black-sync-${variant})
    set_tests_properties(macos_sync_${variant} PROPERTIES TIMEOUT 10)
endforeach()

# Exercise the same context code as NV2A without loading any retail game.
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLACK_SDL REQUIRED IMPORTED_TARGET sdl3)
set(BLACK_GLO "${BLACK_MAC_RUNTIME}/third_party/xemu_nv2a/hw/xbox/nv2a/pgraph/thirdparty/gloffscreen")
if(XEMU_ANGLE_LIB_DIR)
    add_executable(black-context-probe tools/macos/context_probe.c
        "${BLACK_MAC_RUNTIME}/third_party/xemu_nv2a/angle_context.c")
    target_compile_definitions(black-context-probe PRIVATE XBOXRECOMP_ANGLE=1)
    target_include_directories(black-context-probe PRIVATE
        "${BLACK_MAC_RUNTIME}/third_party/angle_headers"
        "${BLACK_MAC_RUNTIME}/third_party/xemu_nv2a")
    target_link_libraries(black-context-probe PRIVATE
        "${XEMU_ANGLE_LIB_DIR}/libEGL.dylib" "${XEMU_ANGLE_LIB_DIR}/libGLESv2.dylib")
else()
    pkg_check_modules(BLACK_EPOXY REQUIRED IMPORTED_TARGET epoxy)
    add_executable(black-context-probe tools/macos/context_probe.c "${BLACK_GLO}/sdl.c")
    target_link_libraries(black-context-probe PRIVATE PkgConfig::BLACK_EPOXY "-framework OpenGL")
endif()
target_include_directories(black-context-probe PRIVATE "${BLACK_GLO}")
target_link_libraries(black-context-probe PRIVATE PkgConfig::BLACK_SDL)
# Run explicitly: GUI sessions may be unavailable in headless CI.
add_custom_target(black-graphics-check COMMAND black-context-probe DEPENDS black-context-probe)

if(BLACK_BUILD_GAME)
    file(GLOB BLACK_GENERATED CONFIGURE_DEPENDS "${BLACK_GEN_DIR}/*.c")
    if(NOT BLACK_GENERATED OR NOT EXISTS "${BLACK_GEN_DIR}/recomp_types.h" OR
       NOT EXISTS "${BLACK_GEN_DIR}/provenance.json")
        message(FATAL_ERROR "Game code missing: run python3 tools/macos/black.py generate /path/to/BLACK first. For runtime-only checks use --preset macos-runtime.")
    endif()
    include("${CMAKE_SOURCE_DIR}/cmake/BlackGameOverlay.cmake")
    add_executable(black-recomp
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/main_posix.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_timing.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_aim.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_mouse.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_rumble.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_flash.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_movies.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_mods.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_language.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_audio.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_hud.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_test_unit.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_checkpoint_hooks.c"
        "${BLACK_MAC_GAME}/src/recomp_manual.c"
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides/game_natives.c"
        ${BLACK_GAME_OVERLAY_SOURCES})
    target_include_directories(black-recomp PRIVATE
        "${BLACK_MAC_GAME}/src" "${BLACK_GEN_DIR}" "${BLACK_MAC_RUNTIME}/include")
    target_compile_options(black-recomp PRIVATE -Wall -Wextra -O2)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/ports/macos/overrides/main_posix.c"
        PROPERTIES COMPILE_DEFINITIONS "BLACK_HOST_SOURCE=\"${BLACK_HOST_OVERLAY}\"")
    target_link_libraries(black-recomp PRIVATE xboxrecomp xemu_nv2a
        "-framework Metal" "-framework QuartzCore" "-framework Cocoa" "-framework Foundation")
endif()
