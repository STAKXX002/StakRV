cmake_minimum_required(VERSION 3.16)
project(StakRV VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stakrv
    src/main.cpp
    src/cpu.cpp
    src/disassembler.cpp
    src/tui.cpp
)

target_include_directories(stakrv PRIVATE src)

target_compile_options(stakrv PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -fsanitize=address>
)

target_link_options(stakrv PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)