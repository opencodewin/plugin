cmake_minimum_required(VERSION 3.12.0)
project(mediasource_node)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SKIP_RPATH ON)
set(CMAKE_MACOSX_RPATH 0)
if (POLICY CMP0054)
    cmake_policy(SET CMP0054 NEW)
endif()
if (POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()
if (POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
endif()

find_package(PkgConfig REQUIRED)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(
        FFMPEG IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswresample
        libavfilter
        libswscale
        libavdevice
    )
    pkg_search_module(SDL2 REQUIRED sdl2)
endif(PKG_CONFIG_FOUND)

include_directories(MediaPlayer)

set(PLUGIN MediaSource)
add_library(
    ${PLUGIN}
    SHARED
    ImMediaSourceNode.cpp
    MediaPlayer/MediaInfo.h
    MediaPlayer/MediaPlayer.h
    MediaPlayer/MediaPlayer.cpp
    MediaPlayer/AudioRender.hpp
    MediaPlayer/AudioRender_Impl_Sdl2.hpp
    MediaPlayer/AudioRender_Impl_Sdl2.cpp
    MediaPlayer/FFUtils.h
    MediaPlayer/FFUtils.cpp
)

set(PLUGIN2 MediaSourceSample)
add_library(
    ${PLUGIN2}
    SHARED
    ImMediaSourceSampleNode.cpp
)

add_dependencies(${PLUGIN} ${EXTRA_DEPENDENCE_PROJECT})
set_property(TARGET ${PLUGIN} PROPERTY POSITION_INDEPENDENT_CODE ON)
add_dependencies(${PLUGIN2} ${EXTRA_DEPENDENCE_PROJECT})
set_property(TARGET ${PLUGIN2} PROPERTY POSITION_INDEPENDENT_CODE ON)
set (LINK_LIBS ${EXTRA_DEPENDENCE_LIBRARYS})

if (FFMPEG_FOUND)
    add_compile_definitions(SDK_WITH_FFMPEG)
    set(LINK_LIBS
        ${LINK_LIBS}
        PkgConfig::FFMPEG
    )
endif(FFMPEG_FOUND)

# SDL2 support
if(SDL2_FOUND)
    message(STATUS "[Media Source node with SDL2 support]")
    include_directories(
        ${SDL2_INCLUDE_DIRS}
    )
    set(LINK_LIBS
        ${LINK_LIBS}
        ${SDL2_STATIC_LDFLAGS}
    )
endif(SDL2_FOUND)

target_link_libraries(
    ${PLUGIN}
    ${LINK_LIBS}
)

set_target_properties(
    ${PLUGIN}
    PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/media/"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/media/"
)

target_link_libraries(
    ${PLUGIN2}
    ${LINK_LIBS}
)

set_target_properties(
    ${PLUGIN2}
    PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/media/"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/media/"
)