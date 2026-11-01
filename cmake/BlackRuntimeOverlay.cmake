# Apply narrow, checked edits to a build-local copy. Pinned submodules stay
# untouched; a changed upstream anchor fails configuration instead of guessing.
file(READ "${BLACK_MAC_RUNTIME}/src/platform/win32_compat.c" BLACK_COMPAT_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/platform/win32_compat.c")
function(black_compat_replace old new expected)
    string(REPLACE "${old}" "" stripped "${BLACK_COMPAT_SOURCE}")
    string(LENGTH "${BLACK_COMPAT_SOURCE}" before)
    string(LENGTH "${stripped}" after)
    string(LENGTH "${old}" length)
    math(EXPR count "(${before} - ${after}) / ${length}")
    if(NOT count EQUAL expected)
        message(FATAL_ERROR "Pinned Win32 event overlay anchor changed (${count}, expected ${expected})")
    endif()
    string(REPLACE "${old}" "${new}" edited "${BLACK_COMPAT_SOURCE}")
    set(BLACK_COMPAT_SOURCE "${edited}" PARENT_SCOPE)
endfunction()

# A notification event releases all CURRENT waiters. ResetEvent may clear its
# persistent state before those waiters reacquire the mutex. Preserve the
# completed release with a generation, rather than losing the wake-up.
black_compat_replace("    int             manual_reset;"
    "    int             manual_reset;\n    uint64_t        signal_generation;" 1)
black_compat_replace("    DWORD result = WAIT_OBJECT_0;"
    "    uint64_t generation = o->signal_generation;\n    DWORD result = WAIT_OBJECT_0;" 1)
black_compat_replace("case K_EVENT:  ready = o->signaled; break;"
    "case K_EVENT:  ready = o->signaled || (o->manual_reset && generation != o->signal_generation); break;" 1)
black_compat_replace("    o->signaled = 1;\n    pthread_cond_broadcast(&o->cond);"
    "    o->signaled = 1;\n    ++o->signal_generation;\n    pthread_cond_broadcast(&o->cond);" 2)
set(BLACK_COMPAT_OVERLAY "${CMAKE_BINARY_DIR}/runtime-overlay/win32_compat.c")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-overlay")
set(BLACK_PREVIOUS_COMPAT "")
if(EXISTS "${BLACK_COMPAT_OVERLAY}")
    file(READ "${BLACK_COMPAT_OVERLAY}" BLACK_PREVIOUS_COMPAT)
endif()
if(NOT BLACK_PREVIOUS_COMPAT STREQUAL BLACK_COMPAT_SOURCE)
    file(WRITE "${BLACK_COMPAT_OVERLAY}" "${BLACK_COMPAT_SOURCE}")
endif()
set_property(TARGET platform PROPERTY SOURCES "${BLACK_COMPAT_OVERLAY}")
