# ═══════════════════════════════════════════════════════════════
#  ps2re — PS2 → ARM64 Re-architecture
#
#  Build targets:
#    ps2re       — main executable
#    texconv     — PS2 texture converter tool
#    modelconv   — PS2 model converter tool
#    shaders     — SPIR-V shader compilation
#    test_*      — unit tests
#
#  Platforms:
#    Linux/ARM64  (native)
#    Android/ARM64 (via NDK toolchain)
#    Linux/x86_64 (development/CI, no NEON intrinsics)
# ═══════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.20)
project(ps2re VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)           # needed for __asm__ volatile, etc.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Platform detection ──────────────────────────────────────
if(ANDROID)
    set(PS2RE_PLATFORM "android")
    message(STATUS "ps2re: Building for Android (${CMAKE_ANDROID_ARCH_ABI})")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(PS2RE_PLATFORM "linux-arm64")
    message(STATUS "ps2re: Building for Linux ARM64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(PS2RE_PLATFORM "linux-x86_64")
    message(STATUS "ps2re: Building for Linux x86_64 (development mode)")
else()
    set(PS2RE_PLATFORM "unknown")
    message(WARNING "ps2re: Unknown platform: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ── Common flags ────────────────────────────────────────────
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if(ANDROID)
    # Android NDK toolchain provides its own optimization flags
    # NEON is always available on arm64-v8a
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+simd+crypto")
    endif()

    set(CMAKE_C_FLAGS_DEBUG "-O0 -g -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto")

elseif(PS2RE_PLATFORM STREQUAL "linux-arm64")
    # Native ARM64 build — full ISA features
    set(CMAKE_C_FLAGS_DEBUG
            "-O0 -g -fsanitize=address,undefined -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE
            "-O3 \
         -march=armv8.2-a+fp16+simd+crypto+sve2 \
         -mtune=cortex-x3 \
         -flto \
         -ffunction-sections \
         -fdata-sections \
         -fno-exceptions \
         -DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE
            "-Wl,--gc-sections -Wl,--as-needed -flto")

elseif(PS2RE_PLATFORM STREQUAL "linux-x86_64")
    # x86_64 development build — stubs for NEON intrinsics
    set(CMAKE_C_FLAGS_DEBUG
            "-O0 -g -fsanitize=address,undefined -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE
            "-O3 -flto -ffunction-sections -fdata-sections -DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE
            "-Wl,--gc-sections -Wl,--as-needed -flto")
endif()

# ── Architecture-specific source selection ──────────────────
# On x86_64, NEON math files need stubs. We handle this via
# preprocessor defines in the source files.

# ── Find dependencies ───────────────────────────────────────
if(ANDROID)
    # Android provides Vulkan, audio (AAudio), etc. via NDK
    # No pkg-config needed
    set(PS2RE_ANDROID_LIBS android log aaudio vulkan)
    set(PS2RE_AUDIO_BACKEND "aaudio")

else()
    find_package(PkgConfig REQUIRED)

    # Vulkan
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        pkg_check_modules(VULKAN vulkan>=1.2)
        if(VULKAN_FOUND)
            set(Vulkan_FOUND TRUE)
            set(Vulkan_LIBRARIES ${VULKAN_LIBRARIES})
            set(Vulkan_INCLUDE_DIRS ${VULKAN_INCLUDE_DIRS})
        endif()
    else()
        message(STATUS "Vulkan found: ${Vulkan_LIBRARIES}")
    endif()

    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR
                "Vulkan not found. Install Vulkan SDK or vulkan-dev package.\n"
                "  Ubuntu/Debian: sudo apt install libvulkan-dev\n"
                "  Fedora:        sudo dnf install vulkan-devel\n"
                "  Arch:          sudo pacman -S vulkan-headers vulkan-icd-loader")
    endif()

    # Window system (optional — only needed for display)
    pkg_check_modules(WAYLAND wayland-client>=1.20)
    pkg_check_modules(XCB xcb>=1.14)

    # Audio backend
    pkg_check_modules(ALSA alsa>=1.2)
    if(ALSA_FOUND)
        set(PS2RE_AUDIO_BACKEND "alsa")
        message(STATUS "Audio: ALSA ${ALSA_VERSION}")
    else()
        set(PS2RE_AUDIO_BACKEND "stub")
        message(WARNING "ALSA not found — audio will be a silent stub.\n"
                "  Install: sudo apt install libasound2-dev")
    endif()

    # Threads
    find_package(Threads REQUIRED)
endif()

# ── Source files ────────────────────────────────────────────

set(PS2RE_CORE
        src/async/ring_buffer.c
        src/async/task_graph.c
        src/async/work_queue.c
        src/async/fence.c
        src/memory/arena.c
        src/memory/pool.c
        src/memory/unified_mem.c
        src/math/vec4_neon.c
        src/math/mat4_neon.c
)

set(PS2RE_PS2
        src/ps2/ee.c
        src/ps2/vu0_pipeline.c
        src/ps2/vu1_pipeline.c
        src/ps2/gs_state.c
        src/ps2/gif_channel.c
        src/ps2/dma_engine.c
        src/ps2/vif_unpack.c
)

set(PS2RE_RENDER
        src/render/renderer.c
        src/render/pipeline_cache.c
        src/render/frame_graph.c
        src/render/texture_manager.c
        src/render/gs_emulation.c
)

set(PS2RE_SCHED
        src/sched/frame_scheduler.c
        src/sched/task_builder.c
)

set(PS2RE_AUDIO
        src/audio/mixer.c
        src/audio/output.c
)

set(PS2RE_INPUT
        src/input/pad.c
)

set(PS2RE_APP
        src/main.c
)

# ── Core library (static, used by main + tests) ────────────

add_library(ps2re_core STATIC
        ${PS2RE_CORE}
        ${PS2RE_PS2}
        ${PS2RE_RENDER}
        ${PS2RE_SCHED}
        ${PS2RE_AUDIO}
        ${PS2RE_INPUT}
)

target_include_directories(ps2re_core PUBLIC include)

# Platform-specific defines
target_compile_definitions(ps2re_core PUBLIC
        PS2RE_AUDIO_BACKEND_${PS2RE_AUDIO_BACKEND}=1
)

if(ANDROID)
    target_compile_definitions(ps2re_core PUBLIC PS2RE_ANDROID=1)
elseif(PS2RE_PLATFORM STREQUAL "linux-arm64")
    target_compile_definitions(ps2re_core PUBLIC PS2RE_LINUX=1)
elseif(PS2RE_PLATFORM STREQUAL "linux-x86_64")
    target_compile_definitions(ps2re_core PUBLIC PS2RE_LINUX=1 PS2RE_X86_64=1)
endif()

# Link dependencies
if(ANDROID)
    target_link_libraries(ps2re_core PUBLIC
            pthread
            m
            ${PS2RE_ANDROID_LIBS}
    )
else()
    target_link_libraries(ps2re_core PUBLIC
            Threads::Threads
            m
            ${Vulkan_LIBRARIES}
    )
    target_include_directories(ps2re_core PUBLIC
            ${Vulkan_INCLUDE_DIRS}
    )

    if(WAYLAND_FOUND)
        target_link_libraries(ps2re_core PUBLIC ${WAYLAND_LIBRARIES})
        target_include_directories(ps2re_core PUBLIC ${WAYLAND_INCLUDE_DIRS})
        target_compile_definitions(ps2re_core PUBLIC PS2RE_HAS_WAYLAND=1)
    endif()

    if(XCB_FOUND)
        target_link_libraries(ps2re_core PUBLIC ${XCB_LIBRARIES})
        target_include_directories(ps2re_core PUBLIC ${XCB_INCLUDE_DIRS})
        target_compile_definitions(ps2re_core PUBLIC PS2RE_HAS_XCB=1)
    endif()

    if(ALSA_FOUND)
        target_link_libraries(ps2re_core PUBLIC ${ALSA_LIBRARIES})
        target_include_directories(ps2re_core PUBLIC ${ALSA_INCLUDE_DIRS})
    endif()
endif()

# ── Main executable ─────────────────────────────────────────

add_executable(ps2re ${PS2RE_APP})
target_link_libraries(ps2re PRIVATE ps2re_core)

# ── Shader compilation ──────────────────────────────────────

find_program(GLSLC glslc
        HINTS
        $ENV{VULKAN_SDK}/bin
        ${Vulkan_INCLUDE_DIRS}/../bin
)

if(NOT GLSLC)
    message(WARNING
            "glslc not found — shaders will not be compiled.\n"
            "  Install Vulkan SDK or: sudo apt install glslang-tools\n"
            "  Or set GLSLC=/path/to/glslc")
else()
    message(STATUS "glslc: ${GLSLC}")
endif()

file(GLOB SHADER_VERT_FILES  shaders/vertex/*.vert)
file(GLOB SHADER_FRAG_FILES  shaders/fragment/*.frag)
file(GLOB SHADER_COMP_FILES  shaders/compute/*.comp)
set(ALL_SHADER_FILES
        ${SHADER_VERT_FILES}
        ${SHADER_FRAG_FILES}
        ${SHADER_COMP_FILES}
)

set(SPIRV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/spirv")
file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

if(GLSLC AND ALL_SHADER_FILES)
    set(SPIRV_OUTPUTS "")

    foreach(SHADER ${ALL_SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(OUTPUT "${SPIRV_OUTPUT_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
                OUTPUT ${OUTPUT}
                COMMAND ${GLSLC}
                --target-env=vulkan1.3
                -O
                -Werror
                ${SHADER}
                -o ${OUTPUT}
                DEPENDS ${SHADER}
                COMMENT "GLSLC  ${SHADER_NAME}"
                VERBATIM
        )
        list(APPEND SPIRV_OUTPUTS ${OUTPUT})
    endforeach()

    add_custom_target(shaders ALL DEPENDS ${SPIRV_OUTPUTS})
    add_dependencies(ps2re shaders)

    # Copy shaders to binary directory for runtime access
    add_custom_command(
            TARGET shaders POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SPIRV_OUTPUT_DIR}
            ${CMAKE_BINARY_DIR}/spirv
            COMMENT "Copying SPIR-V shaders"
    )
endif()

# ── Tools ───────────────────────────────────────────────────

add_executable(texconv tools/texconv/main.c)
target_include_directories(texconv PRIVATE include)
target_link_libraries(texconv PRIVATE m)

add_executable(modelconv tools/modelconv/main.c)
target_include_directories(modelconv PRIVATE include)

# ── Tests ───────────────────────────────────────────────────

enable_testing()

# Test source files — each test links against ps2re_core
set(PS2RE_TESTS
        test/test_task_graph.c
        test/test_ring_buffer.c
        test/test_arena.c
        test/test_neon_math.c
)

foreach(TEST_SRC ${PS2RE_TESTS})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} PRIVATE ps2re_core)

    # Register with CTest
    add_test(
            NAME ${TEST_NAME}
            COMMAND ${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    # Set timeout (stress tests may take a while)
    set_tests_properties(${TEST_NAME} PROPERTIES
            TIMEOUT 30
            LABELS "unit"
    )
endforeach()

# ── Convenience targets ─────────────────────────────────────

# Run all tests
add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${PS2RE_TESTS}
        COMMENT "Running all tests"
)

# Run benchmarks only (tests with "benchmark" in name)
add_custom_target(bench
        COMMAND ${CMAKE_CTEST_COMMAND} -L benchmark --output-on-failure
        COMMENT "Running benchmarks"
)

# ── Install rules (optional) ────────────────────────────────

install(TARGETS ps2re texconv modelconv
        RUNTIME DESTINATION bin
)

install(DIRECTORY shaders/
        DESTINATION share/ps2re/shaders
        FILES_MATCHING
        PATTERN "*.vert"
        PATTERN "*.frag"
        PATTERN "*.comp"
)

install(FILES assets/config.json
        DESTINATION share/ps2re
)

# ── Summary ─────────────────────────────────────────────────

message(STATUS "")
message(STATUS "ps2re build configuration:")
message(STATUS "  Platform:       ${PS2RE_PLATFORM}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Vulkan:         ${Vulkan_FOUND}")
message(STATUS "  Audio backend:  ${PS2RE_AUDIO_BACKEND}")
message(STATUS "  ALSA:           ${ALSA_FOUND}")
message(STATUS "  Wayland:        ${WAYLAND_FOUND}")
message(STATUS "  XCB:            ${XCB_FOUND}")
message(STATUS "  glslc:          ${GLSLC}")
message(STATUS "  Tests:          ${CMAKE_CTEST_COMMAND}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")