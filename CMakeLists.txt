cmake_minimum_required(VERSION 3.10)

project(QuantumVk LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(QM_CXX_FLAGS -Wshadow -Wall -Wextra -Wno-comment -Wno-missing-field-initializers -Wno-empty-body -ffast-math)
    if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        set(QM_CXX_FLAGS ${QM_CXX_FLAGS} -Wno-backslash-newline-escape)
    endif()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        message("Enabling SSE3 support.")
        set(QM_CXX_FLAGS ${QM_CXX_FLAGS} -msse3)
    endif()
elseif (MSVC)
    set(QM_CXX_FLAGS /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4309 /wd4005 /MP /DNOMINMAX)
endif()

macro(set_static_lib_output_dir target dir)
	if(TARGET ${target})
		set_target_properties(${target} PROPERTIES CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${dir})
	endif()
endmacro()

macro(set_executable_output_dir target dir)
	if(TARGET ${target})
		set_target_properties(${target} PROPERTIES CMAKE_RUNTIME_OUTPUT_DIRECTORY ${dir})
	endif()
endmacro()

macro(set_sln_folder target folder)
	if(TARGET ${target})
		set_target_properties(${target} PROPERTIES FOLDER ${folder})
	endif()
endmacro()

# global project stuff
# put cmake build stuff in there own groups
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_C_STANDARD 99)

# We are overriding settings in subdirectories.
# Avoid warnings.
if (${CMAKE_VERSION} VERSION_GREATER "3.13.0")
   message("Setting CMake policy CMP0077.")
   cmake_policy(SET CMP0077 NEW)
endif()

# Options
option(QM_VULKAN_MT "Make QuantumVk thread-safe." ON)
# option(QM_VULKAN_FOSSILIZE "Enable support for Fossilize." OFF)
# ------------------------------

# -----------Volk---------------
add_library(volk STATIC extern/volk/volk.c extern/volk/volk.h)
set_static_lib_output_dir(volk ${CMAKE_CURRENT_SOURCE_DIR}/bin/volk)
if (NOT WIN32)
    target_link_libraries(volk PRIVATE dl)
endif()

target_include_directories(volk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/volk)
target_include_directories(volk PUBLIC "$ENV{VULKAN_SDK}/Include")
set_target_properties(volk PROPERTIES FOLDER Dependencies)

if (WIN32)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
# -------------------------------

# --------SPIRV-Cross------------
# override spirv settings
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE INTERNAL "" FORCE)

set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL ON CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL ON CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL ON CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP ON CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT ON CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL ON CACHE INTERNAL "" FORCE)

set(SPIRV_CROSS_SANITIZE_ADDRESS OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_SANITIZE_MEMORY OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_SANITIZE_THREADS OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_SANITIZE_UNDEFINED OFF CACHE INTERNAL "" FORCE)

set(SPIRV_CROSS_NAMESPACE_OVERRIDE "" CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_FORCE_STL_TYPES OFF CACHE INTERNAL "" FORCE)

set(SPIRV_CROSS_SKIP_INSTALL ON CACHE INTERNAL "" FORCE)

set(SPIRV_CROSS_WERROR OFF CACHE INTERNAL "" FORCE)
set(SPIRV_CROSS_MISC_WARNINGS OFF CACHE INTERNAL "" FORCE)

set(SPIRV_CROSS_FORCE_PIC OFF CACHE INTERNAL "" FORCE)

# Sets to not found. These are only used if testing is enabled, which it isn't.
set(spirv-cross-glslang "NOTFOUND" CACHE INTERNAL "" FORCE)
set(spirv-cross-spirv-as "NOTFOUND" CACHE INTERNAL "" FORCE)
set(spirv-cross-spirv-val "NOTFOUND" CACHE INTERNAL "" FORCE)
set(spirv-cross-spirv-opt "NOTFOUND" CACHE INTERNAL "" FORCE)

add_subdirectory(extern/SPIRV-Cross EXCLUDE_FROM_ALL)

set_sln_folder(spirv-cross SPIRV-Cross)
set_sln_folder(spirv-cross-core SPIRV-Cross)
set_sln_folder(spirv-cross-glsl SPIRV-Cross)
set_sln_folder(spirv-cross-cpp SPIRV-Cross)
set_sln_folder(spirv-cross-hlsl SPIRV-Cross)
set_sln_folder(spirv-cross-msl SPIRV-Cross)
set_sln_folder(spirv-cross-reflect SPIRV-Cross)
set_sln_folder(spirv-cross-util SPIRV-Cross)
# set_target_properties(spirv-cross-core PROPERTIES FOLDER Dependencies)
# -------------------------------

set(QM_VK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# QuantumVk Library
add_library(QuantumVk STATIC)
set_static_lib_output_dir(QuantumVk ${CMAKE_CURRENT_SOURCE_DIR}/bin/QuantumVk)
# Include Directories
target_include_directories(QuantumVk PUBLIC "$ENV{VULKAN_SDK}/Include")
target_include_directories(QuantumVk PUBLIC ${QM_VK_INCLUDE_DIR})
target_include_directories(QuantumVk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# target_compile_options(QuantumVk PRIVATE ${QM_CXX_FLAGS})

# libraries that QuantumVk links to
target_link_libraries(QuantumVk PUBLIC volk PRIVATE spirv-cross-core)
# target_link_libraries(QuantumVk PRIVATE spirv-cross-core)

# Src Directories
set(QM_BASE_DIR src/base)
set(QM_UTILS_DIR src/utils)
set(QM_VK_DIR src/vulkan)

target_sources(QuantumVk PRIVATE 
	${QM_BASE_DIR}/base.cpp
	${QM_BASE_DIR}/base.hpp
	${QM_BASE_DIR}/global.cpp
	${QM_BASE_DIR}/global.hpp
	
	${QM_UTILS_DIR}/aligned_alloc.cpp
	${QM_UTILS_DIR}/aligned_alloc.hpp
	${QM_UTILS_DIR}/compile_time_hash.hpp
	${QM_UTILS_DIR}/enum_cast.hpp
	${QM_UTILS_DIR}/hash.hpp
	${QM_UTILS_DIR}/hashmap.hpp
	${QM_UTILS_DIR}/intrusive.hpp
	${QM_UTILS_DIR}/intrusive_list.hpp
	${QM_UTILS_DIR}/logging.hpp
	${QM_UTILS_DIR}/object_pool.hpp
	${QM_UTILS_DIR}/read_write_lock.hpp
	${QM_UTILS_DIR}/small_vector.hpp
	${QM_UTILS_DIR}/stack_allocator.hpp
	${QM_UTILS_DIR}/string_helpers.cpp
	${QM_UTILS_DIR}/string_helpers.hpp
	${QM_UTILS_DIR}/temporary_hashmap.hpp
	${QM_UTILS_DIR}/timer.cpp
	${QM_UTILS_DIR}/timer.hpp
	${QM_UTILS_DIR}/variant.hpp
	
	${QM_VK_DIR}/context.cpp
	${QM_VK_DIR}/context.hpp
	${QM_VK_DIR}/command_pool.hpp
	${QM_VK_DIR}/cookie.hpp
	${QM_VK_DIR}/device.hpp
	${QM_VK_DIR}/fence.hpp
	${QM_VK_DIR}/format.hpp
	${QM_VK_DIR}/limits.hpp
	${QM_VK_DIR}/quirks.hpp
	${QM_VK_DIR}/texture_format.cpp
	${QM_VK_DIR}/texture_format.hpp
	${QM_VK_DIR}/vulkan_common.hpp
	${QM_VK_DIR}/vulkan_headers.hpp
	
	src/quantumvk.hpp)
	
if (QM_VULKAN_MT)
    target_compile_definitions(QuantumVk PUBLIC QM_VULKAN_MT)
	target_sources(QuantumVk PRIVATE ${QM_VK_DIR}/thread_id.hpp ${QM_VK_DIR}/thread_id.cpp)
endif()

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(QuantumVk PUBLIC VULKAN_DEBUG)
endif()

if (WIN32)
	target_compile_definitions(QuantumVk PRIVATE QM_PLATFORM_WINDOWS)
	target_compile_definitions(QuantumVk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
	
# --------------------------------

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests/example)
set_executable_output_dir(Example ${CMAKE_CURRENT_SOURCE_DIR}/bin/Example)

if(MSVC)
	set(VS_STARTUP_PROJECT Example)
endif()

