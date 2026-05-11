cmake_minimum_required(VERSION 3.16)
project(gbcamera C CXX)

include(CheckIPOSupported)
include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()
# Default to a small-binary profile: MinSizeRel + section-based dead
# stripping + post-link symbol stripping. Flip GBRECOMP_SHRINK off for
# debugging; bump GBRECOMP_GENERATED_OPT_LEVEL to 3 for max perf.
option(GBRECOMP_SHRINK "Aggressive size shrink (dead-strip + symbol strip)" ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
    if(GBRECOMP_SHRINK)
        add_compile_options(-ffunction-sections -fdata-sections)
        add_link_options(-Wl,--gc-sections)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_link_options(-s)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# gb-recompiled runtime
#
# Turn on the camera build path in gbrt before the subdir is added — this
# cart needs the MBC $FC webcam capture, which pulls in OpenCV. Compilations
# that bundle multiple games can also flip this on if any included cart
# needs it.
# ---------------------------------------------------------------------------
set(GBRT_ENABLE_GBCAM ON CACHE BOOL "Compile Game Boy Camera webcam backend" FORCE)

if(NOT TARGET gbrt)
    set(GBRT_REF "main" CACHE STRING "git ref of GB-Recomp/gb-recompiled to use")
    FetchContent_Declare(gb_recompiled
        GIT_REPOSITORY https://github.com/GB-Recomp/gb-recompiled.git
        GIT_TAG ${GBRT_REF}
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(gb_recompiled)
    if(NOT gb_recompiled_POPULATED)
        FetchContent_Populate(gb_recompiled)
    endif()
    add_subdirectory(${gb_recompiled_SOURCE_DIR}/runtime ${CMAKE_BINARY_DIR}/_gbrt_build)
endif()

# ---------------------------------------------------------------------------
# Cart sources
# ---------------------------------------------------------------------------
set(GBCAMERA_SOURCES
    gbcamera_main.c
    gbcamera.c
    gbcamera_rom.c
    gbcamera_dispatch_chunk_0.c
    gbcamera_dispatch_chunk_1.c
    gbcamera_dispatch_chunk_2.c
    gbcamera_funcs_0.c
    gbcamera_funcs_1.c
    gbcamera_funcs_2.c
    gbcamera_funcs_3.c
    gbcamera_funcs_4.c
    gbcamera_funcs_5.c
    gbcamera_funcs_6.c
    gbcamera_funcs_7.c
    gbcamera_funcs_8.c
    gbcamera_funcs_9.c
    gbcamera_funcs_10.c
    gbcamera_funcs_11.c
    gbcamera_funcs_12.c
    gbcamera_funcs_13.c
    gbcamera_funcs_14.c
    gbcamera_funcs_15.c
    gbcamera_funcs_16.c
    gbcamera_funcs_17.c
    gbcamera_funcs_18.c
    gbcamera_funcs_19.c
    gbcamera_funcs_20.c
    gbcamera_funcs_21.c
    gbcamera_funcs_22.c
    gbcamera_funcs_23.c
    gbcamera_funcs_24.c
    gbcamera_funcs_25.c
    gbcamera_funcs_26.c
    gbcamera_funcs_27.c
    gbcamera_funcs_28.c
    gbcamera_funcs_29.c
    gbcamera_funcs_30.c
    gbcamera_funcs_31.c
    gbcamera_funcs_32.c
    gbcamera_funcs_33.c
    gbcamera_funcs_34.c
)

set(GBRECOMP_GENERATED_OPT_LEVEL "1" CACHE STRING "Optimization level for generated ROM source files")
set_property(CACHE GBRECOMP_GENERATED_OPT_LEVEL PROPERTY STRINGS 0 1 2 3)
set_source_files_properties(${GBCAMERA_SOURCES} PROPERTIES COMPILE_OPTIONS "-O${GBRECOMP_GENERATED_OPT_LEVEL}")

# ---------------------------------------------------------------------------
# gbcamera_cart: static library so umbrella compilations can link us in.
# ---------------------------------------------------------------------------
add_library(gbcamera_cart STATIC ${GBCAMERA_SOURCES})
target_include_directories(gbcamera_cart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gbcamera_cart PUBLIC gbrt)

# ---------------------------------------------------------------------------
# Standalone launcher executable.
# ---------------------------------------------------------------------------
if(CMAKE_PROJECT_NAME STREQUAL "gbcamera")
    add_executable(gbcamera_launcher launcher_main.cpp)
    target_link_libraries(gbcamera_launcher PRIVATE gbcamera_cart gbrt)
    set_target_properties(gbcamera_launcher PROPERTIES OUTPUT_NAME gbcamera)
endif()
