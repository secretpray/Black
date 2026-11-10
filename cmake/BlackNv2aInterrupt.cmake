# Wrap the memory source selected by the real-DSP layer, or the pinned source
# when DSP is disabled. Do not lose its existing MMIO changes.
get_target_property(memory_sources xbox_kernel SOURCES)
set(nv2a_memory_source "")
foreach(source IN LISTS memory_sources)
    get_filename_component(name "${source}" NAME)
    if(name STREQUAL "xbox_memory_layout.c")
        if(NOT IS_ABSOLUTE "${source}")
            set(source "${BLACK_MAC_RUNTIME}/src/kernel/${source}")
        endif()
        set(nv2a_memory_source "${source}")
    endif()
endforeach()
if(NOT nv2a_memory_source)
    message(FATAL_ERROR "NV2A memory source was not found")
endif()
# The acknowledgement thread ends each poll with Sleep(0), which on POSIX is
# sched_yield: a 5 s in-mission sample put 586 of its 918 samples in
# cthread_yield, a core held for the whole run. A short sleep keeps the guest's
# GET within 200 us of its PUT without the spin. The anchor is the thread's own
# trailing yield; the busy-bit thread above it keeps its 20 us cadence.
file(READ "${nv2a_memory_source}" nv2a_memory_polled)
set(nv2a_ack_anchor "        Sleep(0);  /* yield; the waiter is spinning on another core */")
string(REPLACE "${nv2a_ack_anchor}" "" nv2a_ack_stripped "${nv2a_memory_polled}")
string(LENGTH "${nv2a_memory_polled}" nv2a_ack_before)
string(LENGTH "${nv2a_ack_stripped}" nv2a_ack_after)
string(LENGTH "${nv2a_ack_anchor}" nv2a_ack_anchor_length)
math(EXPR nv2a_ack_count "(${nv2a_ack_before} - ${nv2a_ack_after}) / ${nv2a_ack_anchor_length}")
if(NOT nv2a_ack_count EQUAL 1)
    message(FATAL_ERROR "Pinned NV2A ack-thread yield anchor changed (${nv2a_ack_count}, expected 1)")
endif()
string(REPLACE "${nv2a_ack_anchor}"
"#if defined(_WIN32)
${nv2a_ack_anchor}
#else
        {
            struct timespec ack_sleep = { 0, 200000 };   /* 200 us */
            nanosleep(&ack_sleep, NULL);
        }
#endif"
    nv2a_memory_polled "${nv2a_memory_polled}")
set(nv2a_memory_patched "${CMAKE_BINARY_DIR}/runtime-overlay/xbox_memory_layout_ack.c")
file(WRITE "${nv2a_memory_patched}" "${nv2a_memory_polled}")
set(nv2a_memory_source "${nv2a_memory_patched}")
list(FILTER memory_sources EXCLUDE REGEX "(^|/)xbox_memory_layout[.]c$")
set_property(TARGET xbox_kernel PROPERTY SOURCES ${memory_sources}
    "${CMAKE_SOURCE_DIR}/ports/macos/overrides/nv2a_memory.c")
set_source_files_properties("${CMAKE_SOURCE_DIR}/ports/macos/overrides/nv2a_memory.c"
    TARGET_DIRECTORY xbox_kernel PROPERTIES
    COMPILE_DEFINITIONS "BLACK_NV2A_MEMORY_SOURCE=\"${nv2a_memory_source}\"")

# Tests compile the actual pinned publication/polling function, with a small
# register bank and a guest ISR following BLACK's W1C/read/FIFO_ACCESS order.
file(READ "${BLACK_MAC_RUNTIME}/src/kernel/xbox_memory_layout.c" nv2a_memory)
black_apu_extract_function(nv2a_memory
    "int xbox_Nv2aSoftwareInterrupt(uint32_t trapped_addr, uint32_t trapped_data,\n                               uint32_t nsource)\n{" nv2a_original)
string(REPLACE "int xbox_Nv2aSoftwareInterrupt(" "int black_OriginalNv2aSoftwareInterrupt("
    nv2a_original "${nv2a_original}")
file(WRITE "${CMAKE_BINARY_DIR}/runtime-overlay/nv2a_software_irq_test.inc" "${nv2a_original}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/kernel/xbox_memory_layout.c")
foreach(variant IN ITEMS fixed baseline)
    add_executable(black-nv2a-fifo-${variant} tools/macos/nv2a_fifo_test.c)
    target_include_directories(black-nv2a-fifo-${variant} PRIVATE
        "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
    target_compile_definitions(black-nv2a-fifo-${variant} PRIVATE
        BLACK_NV2A_IRQ_SOURCE="${CMAKE_BINARY_DIR}/runtime-overlay/nv2a_software_irq_test.inc")
    target_link_libraries(black-nv2a-fifo-${variant} PRIVATE Threads::Threads)
    if(variant STREQUAL "baseline")
        target_compile_definitions(black-nv2a-fifo-${variant} PRIVATE BLACK_NV2A_FIFO_BASELINE=1)
        set(cases handoff delayed)
    else()
        set(cases handoff delayed immediate disabled)
    endif()
    foreach(case IN LISTS cases)
        add_test(NAME macos_nv2a_fifo_${case}_${variant} COMMAND black-nv2a-fifo-${variant} ${case})
        set_tests_properties(macos_nv2a_fifo_${case}_${variant} PROPERTIES TIMEOUT 5)
        if(variant STREQUAL "baseline")
            set_tests_properties(macos_nv2a_fifo_${case}_${variant} PROPERTIES WILL_FAIL TRUE)
        endif()
    endforeach()
endforeach()
