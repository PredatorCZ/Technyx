cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
project(TechnyxToolset)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wl,--no-allow-shlib-undefined")

option(CLI "" ON)
option(GLTF "" ON)

set(EXPOSE_SYMBOLS spike;gltf)

set(TPD_PATH ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(${TPD_PATH}/spike ${CMAKE_BINARY_DIR}/spikelib)
include(targetex)
include(version)
toolset_version()

set_target_properties(spike_cli PROPERTIES OUTPUT_NAME technyx_toolset)

add_spike_subdir(archive)
add_spike_subdir(arcbank)
add_spike_subdir(text)

if (NOT ${CMAKE_BUILD_TYPE} STREQUAL Release)
  add_spike_subdir(dev)
endif()



