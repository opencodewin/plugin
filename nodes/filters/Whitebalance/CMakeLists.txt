cmake_minimum_required(VERSION 3.12.0)
project(whitebalance_node)
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

set(PLUGIN WhiteBalance)

add_library(
    ${PLUGIN}
    SHARED
    ImMatWhitebalanceNode.cpp
)

add_dependencies(${PLUGIN} ${EXTRA_DEPENDENCE_PROJECT})
set_property(TARGET ${PLUGIN} PROPERTY POSITION_INDEPENDENT_CODE ON)
set (LINK_LIBS ${EXTRA_DEPENDENCE_LIBRARYS})

target_link_libraries(
    ${PLUGIN}
    ${LINK_LIBS}
)

set_target_properties(
    ${PLUGIN}
    PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/filters/"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/filters/"
)