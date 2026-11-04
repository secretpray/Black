# Test the exact normal native XPP entry, not the unused experimental USB path.
file(READ "${BLACK_MAC_GAME}/src/recomp_natives.c" native_source)
black_apu_extract_function(native_source "void sub_0027DE6B(void)\n{" rumble_noop)
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/game_rumble_baseline.inc" "${rumble_noop}")
foreach(variant IN ITEMS fixed baseline)
    add_executable(black-game-rumble-${variant} tools/macos/game_rumble_test.c)
    target_include_directories(black-game-rumble-${variant} PRIVATE
        "${BLACK_MAC_RUNTIME}/src" "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
    target_compile_definitions(black-game-rumble-${variant} PRIVATE
        BLACK_CONTROLLER_BACKEND_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/controller_backend_test.inc")
    target_link_libraries(black-game-rumble-${variant} PRIVATE black-port-ui SDL2::SDL2 Threads::Threads)
    target_compile_options(black-game-rumble-${variant} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(black-game-rumble-${variant} PRIVATE -fsanitize=address)
    if(variant STREQUAL "baseline")
        target_compile_definitions(black-game-rumble-${variant} PRIVATE
            BLACK_RUMBLE_BASELINE="${CMAKE_BINARY_DIR}/runtime-overlay/game_rumble_baseline.inc")
    endif()
    add_test(NAME macos_game_rumble_${variant} COMMAND black-game-rumble-${variant})
    set_tests_properties(macos_game_rumble_${variant} PROPERTIES TIMEOUT 5
        ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:exitcode=1:detect_leaks=0")
    if(variant STREQUAL "baseline")
        set_tests_properties(macos_game_rumble_${variant} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()
