cmake_minimum_required(VERSION 3.22)

option(QUEST "Build for quest" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_compile_options(-frtti -fexceptions -fvisibility=hidden -fPIE -fPIC)
# Keep the PC's paths out of __FILE__ in the log lines
get_filename_component(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../core ABSOLUTE)
add_compile_options(-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=mod/ -ffile-prefix-map=${CORE_DIR}/=core/)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qpm.cmake)

if(${CMAKE_BUILD_TYPE} STREQUAL "RELEASE" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo" OR ${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
        add_compile_options(-O3)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        add_compile_options(-flto)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "DEBUG" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
        add_compile_options(-g)
endif()

if(QUEST)
        include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/quest.cmake)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/strip.cmake)

project(octantpath VERSION ${PACKAGE_VERSION})

add_compile_definitions(MOD_ID="${CMAKE_PROJECT_NAME}")
add_compile_definitions(VERSION="${CMAKE_PROJECT_VERSION}")

set(COMPILE_ID ${CMAKE_PROJECT_NAME})

# The core has no game or Quest dependency; it is linked in statically
add_subdirectory(${CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/core)

RECURSE_FILES(cpp_file_list ${SOURCE_DIR}/*.cpp)

add_library(${CMAKE_PROJECT_NAME} SHARED ${cpp_file_list})

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -llog octantpath-core)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${INCLUDE_DIR})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${SHARED_DIR})
