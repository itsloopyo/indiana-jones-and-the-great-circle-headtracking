cmake_minimum_required(VERSION 3.20)
project(GreatCircleHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "TheGreatCircle.exe is 64-bit. Configure with -A x64.")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/cameraunlock-core/vendor/minhook")
if(NOT EXISTS "${MINHOOK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "cameraunlock-core/vendor/minhook is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(${MINHOOK_DIR} minhook)

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

add_library(GreatCircleHeadTracking SHARED
    src/dllmain.cpp
    src/logging.cpp
    src/config.cpp
    src/legacy_config/legacy_config.cpp
    src/hotkeys.cpp
    src/tracker_feed.cpp
    src/headtracking_mod.cpp
    src/camera_hook.cpp
    src/game_state.cpp
    src/hud_view.cpp
    src/window_centering.cpp
    src/builds/build_registry.cpp
    src/builds/gdk_offsets.cpp
)

target_precompile_headers(GreatCircleHeadTracking PRIVATE src/pch.h)

# The version the mod logs comes straight from the project version above, so a
# release stamps CMakeLists.txt and nothing else. A hand-kept copy in a header
# is not in release.ps1's stamp table, and the log then reports 0.0.0 for the
# life of the mod.
target_compile_definitions(GreatCircleHeadTracking PRIVATE
    HEADTRACKING_VERSION_STRING="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(GreatCircleHeadTracking PRIVATE /W4 /WX)
else()
    target_compile_options(GreatCircleHeadTracking PRIVATE -Wall -Wextra -Werror)
endif()

set_target_properties(GreatCircleHeadTracking PROPERTIES
    OUTPUT_NAME "GreatCircleHeadTracking"
    SUFFIX ".asi"
)

target_include_directories(GreatCircleHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(GreatCircleHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
)

# The camera math is header-only, so its test needs no library: it is the one
# part of the mod that can be checked without a running game, and it is the part
# a refactor is most likely to reverse.
enable_testing()

add_executable(camera_math_tests tests/camera_math_tests.cpp)
target_include_directories(camera_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(camera_math_tests PRIVATE /W4 /WX)
endif()
add_test(NAME camera_math COMMAND camera_math_tests)

# The zoom compensation is the one boundary conversion whose failure has no
# symptom of its own: a factor wrong by a constant reads exactly like a factor
# that is right, and the game just feels weak. The suite pins it to the property
# it exists for - the same head angle moving the picture the same distance at
# every field of view - rather than to the arithmetic.
add_executable(zoom_scale_tests tests/zoom_scale_tests.cpp)
target_include_directories(zoom_scale_tests PRIVATE src cameraunlock-core/cpp/include)
if(MSVC)
    target_compile_options(zoom_scale_tests PRIVATE /W4 /WX)
endif()
add_test(NAME zoom_scale COMMAND zoom_scale_tests)

# The interface compensation is two hooks and a page of geometry, and only the
# geometry can be checked without a running game: what a lean does to the view a
# marker is projected through, and where the reticle lands once the aim and the
# picture stop agreeing.
add_executable(hud_math_tests tests/hud_math_tests.cpp)
target_include_directories(hud_math_tests PRIVATE src cameraunlock-core/cpp/include)
if(MSVC)
    target_compile_options(hud_math_tests PRIVATE /W4 /WX)
endif()
add_test(NAME hud_math COMMAND hud_math_tests)

# Where the window centring decides to leave a window alone is pure geometry, and
# it is the half of that feature a running game cannot check: a session shows one
# window size on one monitor, while the cases that matter are the already-centred
# one, the one too large for the work area and the one on a monitor left of the
# primary.
add_executable(window_placement_tests tests/window_placement_tests.cpp)
target_include_directories(window_placement_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(window_placement_tests PRIVATE /W4 /WX)
endif()
add_test(NAME window_placement COMMAND window_placement_tests)

# The default HeadTracking.ini the mod writes on a first launch. It carries the
# config_defaults check src/config.cpp names - the shipped default file read back
# through the frozen reader over a poisoned Config - which is what stops that
# text and the Config member initialisers drifting apart.
add_executable(config_tests tests/config_tests.cpp src/config.cpp src/legacy_config/legacy_config.cpp)
target_include_directories(config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(config_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(config_tests PRIVATE /W4 /WX)
endif()
add_test(NAME config COMMAND config_tests)

# The frozen HeadTracking.ini reader is the input boundary for every file an
# earlier build left: what it accepts, what it refuses, what it puts in place of
# a refusal, and that the refusal is logged rather than swallowed. It reads real
# files through the same GetPrivateProfile* path every published build used.
add_executable(legacy_reader_tests
    tests/config_differential/legacy_reader_tests.cpp
    src/legacy_config/legacy_config.cpp
)
target_include_directories(legacy_reader_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(legacy_reader_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(legacy_reader_tests PRIVATE /W4 /WX)
endif()
add_test(NAME legacy_reader COMMAND legacy_reader_tests)

# The config differential test. The oracle is the newest published build's reader and
# Hotkeys::Start with the core sources they compiled, copied verbatim into
# tests/config_differential/oracle. It is two libraries because Hotkeys::Start has to run on
# oracle_fake's recording poller, keyboard and mod while the reader runs on the real published
# IniReader; each renames the two namespaces its own way, so neither collides with the other or
# with the current core and mod. Neither links the current core, so no current header reaches
# them.
set(GC_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
add_library(gc_config_oracle STATIC
    ${GC_ORACLE_DIR}/src/config.cpp
    ${GC_ORACLE_DIR}/core/src/config/ini_reader.cpp
    ${GC_ORACLE_DIR}/core/src/config/value_guards.cpp
    ${GC_ORACLE_DIR}/core/src/logging/file_log.cpp
    tests/config_differential/oracle_config_adapter.cpp
)
target_include_directories(gc_config_oracle PRIVATE
    ${GC_ORACLE_DIR}/src
    ${GC_ORACLE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
target_compile_definitions(gc_config_oracle PRIVATE
    cameraunlock=gc_oracle_core
    gc_ht=gc_oracle)

add_library(gc_hotkeys_oracle STATIC
    ${GC_ORACLE_DIR}/src/hotkeys.cpp
    ${GC_ORACLE_DIR}/core/src/logging/file_log.cpp
    tests/config_differential/oracle_hotkeys_adapter.cpp
)
target_include_directories(gc_hotkeys_oracle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle_fake
    ${GC_ORACLE_DIR}/src
    ${GC_ORACLE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
target_compile_definitions(gc_hotkeys_oracle PRIVATE
    cameraunlock=gc_oracle_hk_core
    gc_ht=gc_oracle_hk)
target_compile_options(gc_hotkeys_oracle PRIVATE
    /FI${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle_fake/fake_keyboard.h)

add_executable(config_differential_tests
    tests/config_differential/differential_tests.cpp
    src/legacy_config/legacy_config.cpp
)
target_include_directories(config_differential_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential
)
target_compile_definitions(config_differential_tests PRIVATE
    GC_DIFFERENTIAL_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/data")
target_link_libraries(config_differential_tests PRIVATE
    cameraunlock gc_config_oracle gc_hotkeys_oracle)
if(MSVC)
    target_compile_options(config_differential_tests PRIVATE /W4 /WX)
endif()
add_test(NAME config_differential COMMAND config_differential_tests)
