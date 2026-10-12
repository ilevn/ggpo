cmake_minimum_required(VERSION 3.15)

# TODO: SDK Versioning.
project(GGPO VERSION 1.0.0)


if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(src/cmake/properties.cmake)
include(src/cmake/helper_methods.cmake)

if(WIN32)
	## Prevent the CMake trying to install GGPO in Program Files on Windows
	if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		set (CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/installed" CACHE PATH "Default install path" FORCE)
	endif()
endif()

# What do we want to build?
option(GGPO_BUILD_TESTS "Build headless Linux tests" OFF)
option(GGPO_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(GGPO_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "GGPO_SANITIZERS requires GCC or Clang on Linux")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

option(GGPO_BUILD_SDK "Enable the build of the GGPO SDK" ON)
option(GGPO_BUILD_VECTORWAR "Enable the build of the Vector War example app" ON)
option(BUILD_SHARED_LIBS "Enable the build of shared libraries (.dll/.so) instead of static ones (.lib/.a)" ON)

if(GGPO_BUILD_SDK)
	add_subdirectory(src)
endif()

if(GGPO_BUILD_VECTORWAR)
	# Vector War is Windows only.
	if(WIN32)
		add_subdirectory(src/apps/vectorwar)
	else()
		message(WARNING "The Vector War app only supports Windows, skipping...")
	endif()
endif()

if(GGPO_BUILD_TESTS)
    if(NOT GGPO_BUILD_SDK OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Headless tests require the SDK and Linux")
    endif()
    enable_testing()
    add_subdirectory(tests)
endif()
