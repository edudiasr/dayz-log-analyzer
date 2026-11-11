cmake_minimum_required(VERSION 3.16)
project(dayz-log-analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(dayzlog_core STATIC src/adm.cpp src/rpt.cpp src/report.cpp)

add_executable(dayzlog src/main.cpp)
target_link_libraries(dayzlog PRIVATE dayzlog_core)

add_executable(dayzlog_tests tests/tests.cpp)
target_link_libraries(dayzlog_tests PRIVATE dayzlog_core)

enable_testing()
add_test(NAME tests COMMAND dayzlog_tests ${CMAKE_SOURCE_DIR}/examples)
