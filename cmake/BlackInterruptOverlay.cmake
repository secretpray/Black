# Retain pinned bodies, changing only definition names and HAL ownership
# transitions. All references in the bridge resolve to the scoped wrappers.
function(black_interrupt_replace variable anchor replacement)
    string(REPLACE "${anchor}" "" stripped "${${variable}}")
    string(LENGTH "${${variable}}" before)
    string(LENGTH "${stripped}" after)
    string(LENGTH "${anchor}" size)
    math(EXPR count "(${before}-${after})/${size}")
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Pinned interrupt anchor changed: ${anchor}")
    endif()
    string(REPLACE "${anchor}" "${replacement}" edited "${${variable}}")
    set(${variable} "${edited}" PARENT_SCOPE)
endfunction()
file(READ "${BLACK_MAC_RUNTIME}/src/kernel/kernel_bridge.c" irq_bridge)
foreach(pair IN ITEMS "bridge_KeInitializeInterrupt|black_OriginalInitializeInterrupt"
                      "bridge_KeSynchronizeExecution|black_OriginalSynchronizeExecution")
    string(REPLACE "|" ";" names "${pair}")
    list(GET names 0 original)
    list(GET names 1 replacement)
    black_interrupt_replace(irq_bridge "static void ${original}(void)\n{"
        "static void ${replacement}(void)\n{")
endforeach()
black_interrupt_replace(irq_bridge "static int kernel_raise_interrupt(uint32_t vector)\n{"
    "static int black_OriginalRaiseInterrupt(uint32_t vector)\n{")
# A guest NtClose also ends a directory enumeration on that handle
# (BlackFileOverlay.cmake): the next open gets the same handle value back.
black_interrupt_replace(irq_bridge "        HANDLE h = bridge_take_handle(raw_handle);\n        if (h && h != INVALID_HANDLE_VALUE)\n            CloseHandle(h);"
    "        HANDLE h = bridge_take_handle(raw_handle);\n        if (h && h != INVALID_HANDLE_VALUE) {\n            void black_DirForget(HANDLE);\n            black_DirForget(h);\n            CloseHandle(h);\n        }")
set(BLACK_KERNEL_BRIDGE_OVERLAY "${CMAKE_BINARY_DIR}/runtime-overlay/kernel_bridge.c")
file(WRITE "${BLACK_KERNEL_BRIDGE_OVERLAY}" "${irq_bridge}")
file(READ "${BLACK_MAC_RUNTIME}/src/kernel/kernel_hal.c" irq_hal)
black_interrupt_replace(irq_hal "#include \"kernel.h\""
    "#include \"kernel.h\"\n#define BLACK_INTERRUPT_HAL\n#include \"interrupt_gate.h\"")
black_interrupt_replace(irq_hal "    KIRQL old = g_current_irql;\n\n    if (old < DISPATCH_LEVEL"
    "    KIRQL old = g_current_irql;\n\n    if (black_interrupt_depth) {\n        if (new_irql < black_interrupt_floor) {\n            xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, \"ISR attempted to lower below entry IRQL\");\n            return;\n        }\n        g_current_irql = new_irql;\n        return;\n    }\n    if (old < DISPATCH_LEVEL")
set(BLACK_KERNEL_HAL_OVERLAY "${CMAKE_BINARY_DIR}/runtime-overlay/kernel_hal.c")
file(WRITE "${BLACK_KERNEL_HAL_OVERLAY}" "${irq_hal}")
set_source_files_properties("${CMAKE_SOURCE_DIR}/ports/macos/overrides/kernel_bridge.c"
    TARGET_DIRECTORY xbox_kernel
    PROPERTIES COMPILE_DEFINITIONS "BLACK_KERNEL_BRIDGE_SOURCE=\"${BLACK_KERNEL_BRIDGE_OVERLAY}\"")
set_source_files_properties("${CMAKE_SOURCE_DIR}/ports/macos/overrides/kernel_hal.c"
    TARGET_DIRECTORY xbox_kernel
    PROPERTIES COMPILE_DEFINITIONS "BLACK_KERNEL_HAL_SOURCE=\"${BLACK_KERNEL_HAL_OVERLAY}\"")
target_include_directories(xbox_kernel PRIVATE "${CMAKE_SOURCE_DIR}/ports/macos/overrides")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BLACK_MAC_RUNTIME}/src/kernel/kernel_bridge.c"
    "${BLACK_MAC_RUNTIME}/src/kernel/kernel_hal.c")
