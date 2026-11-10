cmake_minimum_required(VERSION 3.21)
project(openeq VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "openeq v1 targets Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENEQ_BUILD_APO   "Build the openeq APO DLL"                ON)
option(OPENEQ_BUILD_CTL   "Build the openeq-devicectl helper"       ON)
option(OPENEQ_BUILD_APP   "Build the Qt user interface"             ON)
option(OPENEQ_BUILD_TESTS "Build tests"                             ON)

# Put every binary in one folder so the app, helper and APO DLL sit side by side.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory(common)
add_subdirectory(core)

if(OPENEQ_BUILD_APO)
    add_subdirectory(apo)
endif()
if(OPENEQ_BUILD_CTL)
    add_subdirectory(devicectl)
endif()
if(OPENEQ_BUILD_APP)
    add_subdirectory(app)
endif()
if(OPENEQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Ship the MSVC runtime DLLs (msvcp140, vcruntime140...) with `cmake --install`.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)
