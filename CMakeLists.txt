cmake_minimum_required(VERSION 3.10)

project(mcvrp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LEMON's headers use APIs deprecated in C++17; silence the header-only noise.
add_compile_options(-Wno-deprecated-declarations)

set(LEMON_INCLUDE_DIR "$ENV{HOME}/lemon/include")
set(LEMON_LIBRARY "$ENV{HOME}/lemon/lib/libemon.a")

if (EXISTS "$ENV{HOME}/or-tools/lib/cmake/ortools/ortoolsConfig.cmake")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/or-tools")
endif()

find_package(ortools CONFIG QUIET)
find_package(Threads REQUIRED)

# Build cluster_first experiments (library + three scenario executables)
file(GLOB_RECURSE CLUSTER_CORE_SRCS
    ${CMAKE_SOURCE_DIR}/cluster_first/*.cpp
)
# Exclude experiment mains from the library to avoid duplicate main definitions
list(FILTER CLUSTER_CORE_SRCS EXCLUDE REGEX ".*/experiments/.*")

add_library(cluster_first_core ${CLUSTER_CORE_SRCS})
target_include_directories(cluster_first_core PUBLIC ${CMAKE_SOURCE_DIR}/cluster_first ${LEMON_INCLUDE_DIR})
target_link_libraries(cluster_first_core PUBLIC ${LEMON_LIBRARY})

add_executable(cluster_first_scenario1 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario1.cpp)
target_link_libraries(cluster_first_scenario1 PRIVATE cluster_first_core)
set_target_properties(cluster_first_scenario1 PROPERTIES
    OUTPUT_NAME scenario1
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cluster_first
)

add_executable(cluster_first_scenario2 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario2.cpp)
target_link_libraries(cluster_first_scenario2 PRIVATE cluster_first_core)
set_target_properties(cluster_first_scenario2 PROPERTIES
    OUTPUT_NAME scenario2
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cluster_first
)

add_executable(cluster_first_scenario3 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario3.cpp)
target_link_libraries(cluster_first_scenario3 PRIVATE cluster_first_core)
set_target_properties(cluster_first_scenario3 PROPERTIES
    OUTPUT_NAME scenario3
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cluster_first
)

# Build match_first experiments (library + three scenario executables)
file(GLOB_RECURSE MATCH_CORE_SRCS
    ${CMAKE_SOURCE_DIR}/match_first/*.cpp
)
list(FILTER MATCH_CORE_SRCS EXCLUDE REGEX ".*/experiments/.*")

add_library(match_first_core ${MATCH_CORE_SRCS})
target_include_directories(match_first_core PUBLIC ${CMAKE_SOURCE_DIR}/match_first ${LEMON_INCLUDE_DIR})
target_link_libraries(match_first_core PUBLIC ${LEMON_LIBRARY})

add_executable(match_first_scenario1 ${CMAKE_SOURCE_DIR}/match_first/experiments/scenario1.cpp)
target_link_libraries(match_first_scenario1 PRIVATE match_first_core)
set_target_properties(match_first_scenario1 PROPERTIES
    OUTPUT_NAME scenario1
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/match_first
)

add_executable(match_first_scenario2 ${CMAKE_SOURCE_DIR}/match_first/experiments/scenario2.cpp)
target_link_libraries(match_first_scenario2 PRIVATE match_first_core)
set_target_properties(match_first_scenario2 PROPERTIES
    OUTPUT_NAME scenario2
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/match_first
)

add_executable(match_first_scenario3 ${CMAKE_SOURCE_DIR}/match_first/experiments/scenario3.cpp)
target_link_libraries(match_first_scenario3 PRIVATE match_first_core)
set_target_properties(match_first_scenario3 PROPERTIES
    OUTPUT_NAME scenario3
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/match_first
)

# Build ca_ilp (combinatorial auction with OR-Tools)
if (ortools_FOUND)
    file(GLOB_RECURSE CA_ILP_CORE_SRCS
        ${CMAKE_SOURCE_DIR}/ca_ilp/*.cpp
    )
    list(FILTER CA_ILP_CORE_SRCS EXCLUDE REGEX ".*/main\\.cpp$")

    add_library(ca_ilp_core ${CA_ILP_CORE_SRCS})
    target_include_directories(ca_ilp_core PUBLIC ${CMAKE_SOURCE_DIR}/ca_ilp)
    target_link_libraries(ca_ilp_core PUBLIC ortools::ortools Threads::Threads)

    add_executable(ca_ilp_main ${CMAKE_SOURCE_DIR}/ca_ilp/main.cpp)
    target_link_libraries(ca_ilp_main PRIVATE ca_ilp_core)
    set_target_properties(ca_ilp_main PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ca_ilp)
else()
    message(WARNING "OR-Tools not found; skipping ca_ilp targets.")
endif()

# Build ca_mis (combinatorial auction with MIS heuristic)
file(GLOB_RECURSE CA_MIS_CORE_SRCS
    ${CMAKE_SOURCE_DIR}/ca_mis/*.cpp
)
list(FILTER CA_MIS_CORE_SRCS EXCLUDE REGEX ".*/main\\.cpp$")

add_library(ca_mis_core ${CA_MIS_CORE_SRCS})
target_include_directories(ca_mis_core PUBLIC ${CMAKE_SOURCE_DIR}/ca_mis)
target_link_libraries(ca_mis_core PUBLIC Threads::Threads)

add_executable(ca_mis_main ${CMAKE_SOURCE_DIR}/ca_mis/main.cpp)
target_link_libraries(ca_mis_main PRIVATE ca_mis_core)
set_target_properties(ca_mis_main PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ca_mis)
