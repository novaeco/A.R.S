cmake_minimum_required(VERSION 3.16)

# Define project version to avoid git describe warning
set(PROJECT_VER "1.0.0")

# Require ESP-IDF to be provided by the idf.py environment instead of relying on
# an implicit IDF_PATH exported in the shell. This makes the build reproducible
# across machines and CI runners.
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not set. Please run the project via 'idf.py' or an ESP-IDF environment shell.")
endif()

message(STATUS "---------------------------------------------------------")
message(STATUS " Build Diagnostics for Reptiles Assistant")
message(STATUS " IDF_PATH: $ENV{IDF_PATH}")
message(STATUS " Project Source: ${CMAKE_SOURCE_DIR}")
message(STATUS "---------------------------------------------------------")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reptiles_assistant VERSION 1.0.0)
