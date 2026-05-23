cmake_minimum_required(VERSION 3.10)

project(mcvrp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LEMON_INCLUDE_DIR "$ENV{HOME}/lemon/include")
set(LEMON_LIBRARY "$ENV{HOME}/lemon/lib/libemon.a")

include(FetchContent)
FetchContent_Declare(
    json
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

find_package(ortools CONFIG REQUIRED)
find_package(OpenMP)

# Build cluster_first experiments (library + three scenario executables)
file(GLOB_RECURSE CLUSTER_CORE_SRCS
    ${CMAKE_SOURCE_DIR}/cluster_first/*.cpp
)
# Exclude experiment mains from the library to avoid duplicate main definitions
list(FILTER CLUSTER_CORE_SRCS EXCLUDE REGEX ".*/experiments/.*")

add_library(cluster_first_core ${CLUSTER_CORE_SRCS})
target_include_directories(cluster_first_core PUBLIC ${CMAKE_SOURCE_DIR}/cluster_first ${LEMON_INCLUDE_DIR})
target_link_libraries(cluster_first_core PUBLIC nlohmann_json::nlohmann_json ${LEMON_LIBRARY})

add_executable(scenario1 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario1.cpp)
target_link_libraries(scenario1 PRIVATE cluster_first_core)
set_target_properties(scenario1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cluster_first)

add_executable(scenario2 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario2.cpp)
target_link_libraries(scenario2 PRIVATE cluster_first_core)
set_target_properties(scenario2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cluster_first)

add_executable(scenario3 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario3.cpp)
target_link_libraries(scenario3 PRIVATE cluster_first_core)
set_target_properties(scenario3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cluster_first)

# Build ca_ilp (combinatorial auction with OR-Tools)
file(GLOB_RECURSE CA_ILP_CORE_SRCS
    ${CMAKE_SOURCE_DIR}/ca_ilp/*.cpp
)
list(FILTER CA_ILP_CORE_SRCS EXCLUDE REGEX ".*/main\\.cpp$")

add_library(ca_ilp_core ${CA_ILP_CORE_SRCS})
target_include_directories(ca_ilp_core PUBLIC ${CMAKE_SOURCE_DIR}/ca_ilp)
target_link_libraries(ca_ilp_core PUBLIC ortools::ortools)
if (OpenMP_CXX_FOUND)
    target_link_libraries(ca_ilp_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(ca_ilp_main ${CMAKE_SOURCE_DIR}/ca_ilp/main.cpp)
target_link_libraries(ca_ilp_main PRIVATE ca_ilp_core)
set_target_properties(ca_ilp_main PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ca_ilp)

# Build ca_mis (combinatorial auction with MIS heuristic)
file(GLOB_RECURSE CA_MIS_CORE_SRCS
    ${CMAKE_SOURCE_DIR}/ca_mis/*.cpp
)
list(FILTER CA_MIS_CORE_SRCS EXCLUDE REGEX ".*/(main|cvrp_ca_mis_fixed)\\.cpp$")

add_library(ca_mis_core ${CA_MIS_CORE_SRCS})
target_include_directories(ca_mis_core PUBLIC ${CMAKE_SOURCE_DIR}/ca_mis)
if (OpenMP_CXX_FOUND)
    target_link_libraries(ca_mis_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(ca_mis_main ${CMAKE_SOURCE_DIR}/ca_mis/main.cpp)
target_link_libraries(ca_mis_main PRIVATE ca_mis_core)
set_target_properties(ca_mis_main PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ca_mis)


