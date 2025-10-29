# Copyright (c) 2021 The Logos Foundation

cmake_minimum_required(VERSION 3.15)

project(nng_interface)

add_library(nng_interface
    nng_interface.cpp
)

# Find NNG library
find_package(nng CONFIG REQUIRED)

# Find flatbuffers library
find_package(Flatbuffers CONFIG REQUIRED)

# Find flatc executable
# During cross-compilation, we need to ensure we find the native flatc,
# not the cross-compiled one. The key is using NO_CMAKE_FIND_ROOT_PATH
# to avoid finding binaries from the target architecture.
if(CMAKE_CROSSCOMPILING)
	# First try system PATH with NO_CMAKE_FIND_ROOT_PATH to skip target paths
	find_program(_FLATC flatc NO_CMAKE_FIND_ROOT_PATH)
	if(NOT _FLATC AND CMAKE_PREFIX_PATH)
		# If not in system PATH, try CMAKE_PREFIX_PATH (native deps location)
		list(GET CMAKE_PREFIX_PATH 0 NATIVE_PREFIX)
		find_program(_FLATC flatc PATHS "${NATIVE_PREFIX}/bin" NO_CMAKE_FIND_ROOT_PATH)
	endif()
	# Final fallback: normal search (should respect CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER)
	if(NOT _FLATC)
		find_program(_FLATC flatc)
	endif()
else()
	# For native builds, just search normally
	find_program(_FLATC flatc)
endif()
if(NOT _FLATC)
	message(FATAL_ERROR "flatc executable not found. Required for building nng_interface.")
endif()
message(STATUS "Using flatc executable: ${_FLATC}")

# Location of the .fbs file for the NNG interface
get_filename_component(FLATBUFFERS_FILE "nng_interface.fbs" ABSOLUTE)

# Set output paths (determined by flatc's naming conventions)
set(GENFLATBUF_FOLDER "${CMAKE_CURRENT_BINARY_DIR}")
set(NNG_FLATBUF_H "${GENFLATBUF_FOLDER}/nng_interface_generated.h")
set(NNG_FLATBUF_PY "${GENFLATBUF_FOLDER}/NngInterface")

# Generate Python and CPP files for NNG interface flatbuffers
add_custom_command(
    OUTPUT "${NNG_FLATBUF_H}" "${NNG_FLATBUF_PY}"
    COMMAND ${_FLATC}
    ARGS -o "${GENFLATBUF_FOLDER}"
        --cpp
        --python
        "${FLATBUFFERS_FILE}"
    DEPENDS "${FLATBUFFERS_FILE}")

# Custom target for flatbuf files, so they get re-generated
add_custom_target(nng_files ALL
    DEPENDS "${NNG_FLATBUF_H}" "${NNG_FLATBUF_PY}")

# Include generated *_generated.h files
include_directories("${GENFLATBUF_FOLDER}")

# Add custom target as dependency
add_dependencies(nng_interface nng_files)

# Add required libraries to nng_interface
target_link_libraries(nng_interface
    nng::nng
    util
    leveldb)
