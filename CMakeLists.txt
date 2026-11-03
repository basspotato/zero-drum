cmake_minimum_required(VERSION 3.22)

project(ZeroDrum VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(ZD_BUILD_PLUGIN "Build the JUCE plugin shell (CLAP, VST3, AU, Standalone)" ON)
option(ZD_BUILD_TESTS "Build the engine test suite" ON)

include(cmake/Dependencies.cmake)

# The engine is framework-free: it must never depend on JUCE.
add_subdirectory(engine)

if(ZD_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

if(ZD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Offline renderer: renders the default beat to WAV with no JUCE dependency.
add_executable(zd_render tools/render.cpp)
target_link_libraries(zd_render PRIVATE zd_engine)
