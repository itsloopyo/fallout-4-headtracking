cmake_minimum_required(VERSION 3.20)
project(Fallout4HeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MinHook (vendored). Built as its own target so cameraunlock_hooks can link
# against it; the objects end up statically linked into the .asi.
add_library(minhook STATIC
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

# Shared CameraUnlock C++ core (submodule)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Mod sources.
set(MOD_SOURCES
    src/pch.cpp
    src/dllmain.cpp
    src/lean_trace.cpp
    src/hooks/ads_lean.cpp
    src/core/mod.cpp
    src/core/config.cpp
    src/legacy_config/legacy_config.cpp
    src/core/hotkeys.cpp
    src/core/path_utils.cpp
    src/core/seh_guard.cpp
    src/core/session_end.cpp
    src/diagnostics/ab_switches.cpp
    src/diagnostics/frame_verdict.cpp
    src/diagnostics/matrix_dump.cpp
    src/diagnostics/pose_trace.cpp
    src/diagnostics/render_audit.cpp
    src/diagnostics/vats_probe.cpp
    src/diagnostics/write_watch.cpp
    src/hooks/aim_decoupling.cpp
    src/hooks/camera_hook.cpp
    src/hooks/camera_math.cpp
    src/hooks/camera_snapshot.cpp
    src/hooks/crosshair_hook.cpp
    src/hooks/hit_marker_hook.cpp
    src/hooks/crosshair_layout.cpp
    src/hooks/hook_slot.cpp
    src/hooks/module_scan.cpp
    src/hooks/player_hook.cpp
    src/hooks/view_matrix_hook.cpp
    src/game/fov_settings.cpp
    src/game/game_state.cpp
    src/ui/game_window.cpp
    src/ui/notification.cpp
    # RTTI vtable resolver from cameraunlock-core. It lives in the optional
    # cameraunlock_discovery target (which also pulls in the MinHook-based
    # CameraDiscovery class we don't use), so compile just this TU here. It
    # only depends on pattern_scanner (in the linked cameraunlock lib).
    cameraunlock-core/cpp/src/memory/rtti_vtable.cpp
)

add_library(Fallout4HeadTracking SHARED ${MOD_SOURCES})

set_source_files_properties(
    cameraunlock-core/cpp/src/memory/rtti_vtable.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
target_precompile_headers(Fallout4HeadTracking PRIVATE src/pch.h)

# Output as .asi for Ultimate ASI Loader
set_target_properties(Fallout4HeadTracking PROPERTIES
    OUTPUT_NAME "Fallout4HeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(Fallout4HeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
)

target_include_directories(Fallout4HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(Fallout4HeadTracking PRIVATE
        ws2_32
        Psapi
    )
endif()

# Opt-in unit tests for the mod code that can run outside the game: the config
# boundary and the head-tracking maths. OFF by default so neither the normal
# build nor the release packager ever drops a test binary into bin/. Enable with
option(FALLOUT4_DEV_HOTKEYS "Build the diagnostic hotkeys (developer aid, not for release)" OFF)
if(FALLOUT4_DEV_HOTKEYS)
    target_compile_definitions(Fallout4HeadTracking PRIVATE FALLOUT4_DEV_HOTKEYS=1)
    message(WARNING "FALLOUT4_DEV_HOTKEYS is ON. This build registers twelve "
                    "reverse-engineering instruments on Ctrl+Shift+D/J/I/K/B/V/X/N/M/W/O "
                    "and Insert: pose and verdict trace dumps, A/B switches, VATS probes, "
                    "hardware write watches on the camera root rotations, and a matrix "
                    "dump that starts a render-audit sampler thread. Do not ship it.")
endif()

# -DFALLOUT4_BUILD_TESTS=ON, or run 'pixi run test'.
option(FALLOUT4_BUILD_TESTS "Build Fallout4HeadTracking unit tests" OFF)
if(FALLOUT4_BUILD_TESTS)
    enable_testing()

    # Every test is named for its target and hangs off this aggregate, so
    # run-tests.ps1 builds them all with one --target and selects them all with
    # one ctest filter. Both matter: the build tree also configures the
    # submodule's own tests, which this script neither builds nor runs.
    add_custom_target(fallout4_tests)

    function(fallout4_add_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${name} PRIVATE cameraunlock)
        set_target_properties(${name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /WX /permissive-)
        endif()
        add_test(NAME ${name} COMMAND ${name})
        add_dependencies(fallout4_tests ${name})
    endfunction()

    # The committed HeadTracking.ini against the table, and the toggles' saves.
    # `--render-config <path>` makes it write the committed file instead
    # (pixi run render-config).
    fallout4_add_test(fallout4_config_tests
        tests/config_tests.cpp
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_compile_definitions(fallout4_config_tests PRIVATE
        F4_COMMITTED_CONFIG=L"${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")

    # The config differential test. The oracle is the dev pre-release's reader (fbefff4), the
    # only published build, copied byte for byte into tests/config_differential/oracle; renaming
    # its namespace lets it link beside this build's. oracle_adapter.cpp copies its Config out
    # for the test. Every core source it compiles is the same bytes at that build's pin.
    set(F4_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(f4_config_oracle STATIC
        ${F4_ORACLE_DIR}/src/core/config.cpp
        tests/config_differential/oracle_adapter.cpp
    )
    target_include_directories(f4_config_oracle PRIVATE
        ${F4_ORACLE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(f4_config_oracle PRIVATE Fallout4HT=f4_oracle)
    target_link_libraries(f4_config_oracle PRIVATE cameraunlock)

    fallout4_add_test(fallout4_config_differential
        tests/config_differential/differential_tests.cpp
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(fallout4_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(fallout4_config_differential PRIVATE
        F4_DIFFERENTIAL_INPUTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/inputs"
        F4_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/HeadTracking.ini")
    target_link_libraries(fallout4_config_differential PRIVATE f4_config_oracle)

    # Core's canonical config lint over the committed file and every file the differential
    # test migrated, which it writes beside itself under migrated\.
    add_test(NAME fallout4_config_lint COMMAND node
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:fallout4_config_differential>/migrated)
    set_tests_properties(fallout4_config_lint PROPERTIES DEPENDS fallout4_config_differential)

    # The frozen reader, the core sources it compiles, the oracle and the inputs against the
    # hashes tests/config_differential/frozen.tsv records.
    add_test(NAME fallout4_config_frozen COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/check-frozen.ps1)
    set_tests_properties(fallout4_config_differential fallout4_config_lint fallout4_config_frozen
        PROPERTIES LABELS differential)

    fallout4_add_test(fallout4_fov_tests
        tests/fov_tests.cpp
    )
    fallout4_add_test(fallout4_collision_tests tests/collision_tests.cpp)

    fallout4_add_test(fallout4_camera_math_tests
        tests/camera_math_tests.cpp
        src/hooks/camera_math.cpp
        src/hooks/crosshair_layout.cpp
    )

    fallout4_add_test(fallout4_ads_lean_tests
        tests/ads_lean_tests.cpp
        src/hooks/camera_math.cpp
    )
endif()
