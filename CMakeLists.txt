cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Dead or Alive Xtreme Beach Volleyball (Xbox, NTSC-U) - Static Recompilation
#
# Hosts: Windows (MSVC, D3D11), Linux (GCC, SDL2 + OpenGL) and Nintendo
# Switch homebrew (devkitA64 + libnx, built with scripts/switch.sh).
# ============================================================================

project(goon_nx C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # DOAX's image (.data BSS) runs to 0x00EFE420, over the runtime's default
    # area at 0x00700000: its TLS, kernel data and main stack go above it.
    # Contiguous memory comes out of the heap (as on the console, one 64 MB
    # budget), and DOAX uses nearly all of it -- a 21 MB pool, 7 MB and 4 MB
    # buffers, a 7 MB contiguous block on the title screen. The main stack
    # region is 768 KB: the runtime's worker stack slices (timer, USB ISR;
    # 256 KB each) are carved from its low end -- a 256 KB region overlapped
    # them and stalled start-up -- leaving 256 KB to the main thread (its XBE
    # asks for 64 KB). 128 KB per guest thread. Heap 0x01040000-0x04000000.
    XBOX_RUNTIME_BASE=0x00F00000
    XBOX_STACK_SIZE=0xC0000
    XBOX_THREAD_STACK_SIZE=0x20000
)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# The lifted code is ~66 MB of C generated from the retail XBE by
# scripts/regen.sh. It is never committed.
set(GOON_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/work/gen" CACHE PATH
    "Directory holding the tools.recomp output (recomp_*.c, recomp_funcs.h)")

if(NOT EXISTS "${GOON_GEN_DIR}/recomp_funcs.h")
    message(FATAL_ERROR "No generated code in ${GOON_GEN_DIR}. Run "
                        "scripts/regen.sh, or pass -DGOON_GEN_DIR=<dir>.")
endif()

# The Vulkan renderer (xboxrecomp nv2a_vk) instead of OpenGL. On the Switch it
# links mesa-switch's static NVK (NVK_SDK) and builds goon-nx-vulkan.nro.
option(GOON_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${GOON_VULKAN} CACHE BOOL "" FORCE)

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${GOON_GEN_DIR}/*.c")

set(GAME_SOURCES
    src/main.c
    src/platform/switch.c
    game/overrides.c
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

if(GOON_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GOON_VULKAN=1)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GOON_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11 dxgi dxguid xinput winmm dbghelp)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
    endif()
else()
    # The generated chunks are huge straight-line functions; -O1 keeps GCC's
    # memory and time per chunk sane while still allocating registers. The
    # Switch's CPU is the bottleneck there, so it gets -O2.
    if(NINTENDO_SWITCH)
        set(GOON_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
    else()
        set(GOON_GEN_OPT "-O1" CACHE STRING "Optimisation for the lifted code")
    endif()
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${GOON_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv")
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── Nintendo Switch homebrew (devkitA64 + libnx) ────────────────
#
# Build with scripts/switch.sh. The NRO reads the extracted disc from
# sdmc:/switch/goon-nx/game/ (never an ISO); logs and goon_env.txt live in
# sdmc:/switch/goon-nx/.
if(NINTENDO_SWITCH)
    set(GOON_SWITCH_APP_NAME "DOA Xtreme Beach Volleyball (Xbox recomp)" CACHE STRING
        "Title shown in the homebrew menu")
    set(GOON_SWITCH_AUTHOR "goon-nx" CACHE STRING "Author shown in the homebrew menu")
    set(GOON_SWITCH_VERSION "0.1" CACHE STRING "Version shown in the homebrew menu")
    set(GOON_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/game/assets/icon.jpg" CACHE FILEPATH
        "256x256 JPEG icon (libnx default when empty)")

    if(GOON_VULKAN)
        # No GL: SDL is only the audio output; NVK comes with nv2a_vk.
        target_link_libraries(${PROJECT_NAME} PRIVATE nx m)
    else()
        # switch-sdl2's GL path is EGL on switch-mesa.
        # Older switch-mesa links libdrm_nouveau; newer builds carry it in
        # libEGL and need Mesa's utility libraries instead (egl.pc).
        if(XBOXRECOMP_DRM_NOUVEAU)
            target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau nx m)
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi expat stdc++
                mesa_util_c11 blake3 mesa_util mesa_util_simd zstd z nx pthread m)
        endif()
    endif()
    # SDL's audio thread gets a real-time priority and audren gaps are
    # counted (src/platform/switch.c, "SDL's audio thread").
    target_link_options(${PROJECT_NAME} PRIVATE
        "LINKER:--wrap=SDL_SYS_SetThreadPriority"
        "LINKER:--wrap=audrvVoiceAddWaveBuf")
    set_property(SOURCE src/platform/switch.c APPEND PROPERTY
        INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/apu")
    # src/platform/switch.c's loading screen asks EGL for the on-screen size.
    target_include_directories(${PROJECT_NAME} PRIVATE "$ENV{DEVKITPRO}/portlibs/switch/include")

    set(GOON_NACP "${CMAKE_CURRENT_BINARY_DIR}/goon-nx.nacp")
    nx_generate_nacp(OUTPUT "${GOON_NACP}"
        NAME "${GOON_SWITCH_APP_NAME}"
        AUTHOR "${GOON_SWITCH_AUTHOR}"
        VERSION "${GOON_SWITCH_VERSION}")
    if(GOON_SWITCH_ICON)
        nx_create_nro(${PROJECT_NAME} NACP "${GOON_NACP}" ICON "${GOON_SWITCH_ICON}")
    else()
        nx_create_nro(${PROJECT_NAME} NACP "${GOON_NACP}")
    endif()
endif()
