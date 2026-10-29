cmake_minimum_required(VERSION 3.16)
project(visual_graph_tool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(graph_config src/cli.cpp src/validation.cpp)
target_include_directories(graph_config PUBLIC src)

add_executable(visual_graph_tool src/main.cpp)
target_link_libraries(visual_graph_tool PRIVATE graph_config)
if(MINGW)
    target_link_options(visual_graph_tool PRIVATE -municode)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(config_tests tests/test_config.cpp)
    target_link_libraries(config_tests PRIVATE graph_config)
    add_test(NAME config_tests COMMAND config_tests)
    add_test(NAME cli_contract COMMAND ${CMAKE_COMMAND}
        "-DAPP=$<TARGET_FILE:visual_graph_tool>"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/tests/check_cli.cmake")
endif()
