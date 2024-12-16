cmake_minimum_required(VERSION 3.12.0)
project(upscale_anime_slower_node)
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

set(PLUGIN AnimeSlower)
set(PLUGIN_DATA AnimeSlower.data)

add_library(
    ${PLUGIN}
    SHARED
    AnimeSlower.cpp
    AnimeSlower_data.cpp
    AnimeSlower_data.h
)

add_dependencies(${PLUGIN} BluePrintSDK VkShader imgui ncnn)
set_property(TARGET ${PLUGIN} PROPERTY POSITION_INDEPENDENT_CODE ON)
set (LINK_LIBS
    -L${CMAKE_BINARY_DIR}
    BluePrintSDK
    VkShader
    realsr
    ncnn
    imgui
)

target_link_libraries(
    ${PLUGIN}
    ${LINK_LIBS}
)

set_target_properties(
    ${PLUGIN}
    PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/upscale/${PLUGIN}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/upscale/${PLUGIN}"
)

add_custom_command(
    TARGET ${PLUGIN}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/${PLUGIN_DATA} ${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/upscale/${PLUGIN}/
    COMMENT "Copying the data file"
)