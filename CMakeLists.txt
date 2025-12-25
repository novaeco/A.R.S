cmake_minimum_required(VERSION 3.16)

# Force ESP32-S3 target (P0 Fix)
set(IDF_TARGET "esp32s3")

# Define project version to avoid git describe warning
set(PROJECT_VER "1.0.0")

# Resolve ESP-IDF location from either the cache variable or environment to
# avoid brittle environment assumptions when using idf.py.
if(DEFINED IDF_PATH)
    set(_resolved_idf_path "${IDF_PATH}")
elseif(DEFINED ENV{IDF_PATH})
    set(_resolved_idf_path "$ENV{IDF_PATH}")
else()
    message(FATAL_ERROR "ESP-IDF path not set. Run via 'idf.py' (ESP-IDF 6.x) or export IDF_PATH explicitly.")
endif()

message(STATUS "---------------------------------------------------------")
message(STATUS " Build Diagnostics for Reptiles Assistant")
message(STATUS " IDF_PATH: ${_resolved_idf_path}")
message(STATUS " Project Source: ${CMAKE_SOURCE_DIR}")
message(STATUS "---------------------------------------------------------")

include(${_resolved_idf_path}/tools/cmake/project.cmake)
project(reptiles_assistant VERSION 1.0.0)
