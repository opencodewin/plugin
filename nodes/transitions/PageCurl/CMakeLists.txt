cmake_minimum_required(VERSION 3.12.0)
project(pagecurl_node)
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

set(PLUGIN PageCurl)
add_library(
    ${PLUGIN}
    SHARED
    ImMatPageCurlTransitionNode.cpp
)

add_dependencies(${PLUGIN} ${EXTRA_DEPENDENCE_PROJECT})
set (LINK_LIBS ${EXTRA_DEPENDENCE_LIBRARYS})
set_property(TARGET ${PLUGIN} PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(
    ${PLUGIN}
    ${LINK_LIBS}
)

set_target_properties(
    ${PLUGIN}
    PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/fusions/"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_FOLDER}/fusions/"
)