cmake_minimum_required(VERSION 3.30)

project(noble LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NOBLE_BUILD_TUI "Build the optional FTXUI tracing frontend" OFF)



add_subdirectory("src/")
