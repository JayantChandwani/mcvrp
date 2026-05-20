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

add_executable(scenario2 ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario2.cpp)
target_link_libraries(scenario2 PRIVATE cluster_first_core)

add_executable(scenario3_incomp ${CMAKE_SOURCE_DIR}/cluster_first/experiments/scenario3_incomp.cpp)
target_link_libraries(scenario3_incomp PRIVATE cluster_first_core)


