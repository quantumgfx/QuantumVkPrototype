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

macro(set_sln_folder target folder)
	if(TARGET ${target})
		set_target_properties(${target} PROPERTIES FOLDER ${folder})
	endif()
endmacro()

#macro(override_option setting value)
#	set(${setting} ${value} CACHE INTERNAL "" FORCE)
#endmacro()


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

# Get Default vulkan sdk directory
set(DEFAULT_VULKAN_SDK_DIR "$ENV{VULKAN_SDK}")

# Options
option(QM_VULKAN_MT "Make QuantumVk thread-safe." ON)

set(VULKAN_SDK_DIR "$ENV{VULKAN_SDK}")
# ------------------------------

# -----------Volk---------------
add_library(volk STATIC extern/volk/volk.c extern/volk/volk.h)
if (NOT WIN32)
    target_link_libraries(volk PRIVATE dl)
endif()

target_include_directories(volk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/volk)
target_include_directories(volk PUBLIC "${VULKAN_SDK_DIR}/Include")

message("Vulkan Include directory: ${VULKAN_SDK_DIR}/Include")

set_sln_folder(volk Dependencies)

if (WIN32)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
# -------------------------------

add_subdirectory(extern EXCLUDE_FROM_ALL)
# -------------------------------

# QuantumVk Library
add_library(QuantumVk STATIC)
# Include Directories
target_include_directories(QuantumVk PUBLIC "${VULKAN_SDK_DIR}/Include")
target_include_directories(QuantumVk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(QuantumVk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(QuantumVk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern)

# target_compile_options(QuantumVk PRIVATE ${QM_CXX_FLAGS})

# libraries that QuantumVk links to
target_link_libraries(QuantumVk PUBLIC volk)
target_link_libraries(QuantumVk PRIVATE spirv-cross-core)
target_link_libraries(QuantumVk PRIVATE glslang)
target_link_libraries(QuantumVk PRIVATE SPIRV)

target_link_libraries(QuantumVk PRIVATE ${VULKAN_SDK_DIR}/Lib/shaderc.lib)
# target_link_libraries(QuantumVk PRIVATE glslang)

# Src Directories
set(QM_UTILS_DIR src/utils)
set(QM_VK_DIR src/vulkan)
set(QM_THREADING_DIR src/threading)

target_sources(QuantumVk PRIVATE 
	
	${QM_UTILS_DIR}/aligned_alloc.cpp
	${QM_UTILS_DIR}/aligned_alloc.hpp
	${QM_UTILS_DIR}/bitops.hpp
	${QM_UTILS_DIR}/compile_time_hash.hpp
	${QM_UTILS_DIR}/enum_cast.hpp
	${QM_UTILS_DIR}/hash.hpp
	${QM_UTILS_DIR}/hashmap.hpp
	${QM_UTILS_DIR}/intrusive.hpp
	${QM_UTILS_DIR}/intrusive_list.hpp
	${QM_UTILS_DIR}/intrusive_hash_map.hpp
	${QM_UTILS_DIR}/intrusive_object_pool.hpp
	${QM_UTILS_DIR}/logging.hpp
	${QM_UTILS_DIR}/object_pool.hpp
	${QM_UTILS_DIR}/read_write_lock.hpp
	${QM_UTILS_DIR}/retained_heap_data.hpp
	${QM_UTILS_DIR}/small_vector.hpp
	${QM_UTILS_DIR}/stack_allocator.hpp
	${QM_UTILS_DIR}/string_helpers.cpp
	${QM_UTILS_DIR}/string_helpers.hpp
	${QM_UTILS_DIR}/temporary_hashmap.hpp
	${QM_UTILS_DIR}/timer.cpp
	${QM_UTILS_DIR}/timer.hpp
	${QM_UTILS_DIR}/variant.hpp
	
	${QM_THREADING_DIR}/thread_group.hpp ${QM_THREADING_DIR}/thread_group.cpp
	${QM_THREADING_DIR}/thread_id.hpp ${QM_THREADING_DIR}/thread_id.cpp
	
	${QM_VK_DIR}/graphics/descriptor_set.hpp ${QM_VK_DIR}/graphics/descriptor_set.cpp
	${QM_VK_DIR}/graphics/render_pass.hpp ${QM_VK_DIR}/graphics/render_pass.cpp
	${QM_VK_DIR}/graphics/shader.hpp ${QM_VK_DIR}/graphics/shader.cpp
	
	${QM_VK_DIR}/images/format.hpp
	${QM_VK_DIR}/images/image.hpp ${QM_VK_DIR}/images/image.cpp
	${QM_VK_DIR}/images/sampler.hpp ${QM_VK_DIR}/images/sampler.cpp
	${QM_VK_DIR}/images/texture_format.hpp ${QM_VK_DIR}/images/texture_format.cpp
	
	${QM_VK_DIR}/memory/buffer.hpp ${QM_VK_DIR}/memory/buffer.cpp
	${QM_VK_DIR}/memory/buffer_pool.hpp ${QM_VK_DIR}/memory/buffer_pool.cpp
	${QM_VK_DIR}/memory/memory_allocator.hpp ${QM_VK_DIR}/memory/memory_allocator.cpp
	${QM_VK_DIR}/memory/vma_build.cpp
	
	${QM_VK_DIR}/misc/cookie.hpp ${QM_VK_DIR}/misc/cookie.cpp
	${QM_VK_DIR}/misc/limits.hpp
	${QM_VK_DIR}/misc/quirks.hpp
	${QM_VK_DIR}/misc/type_to_string.hpp
	
	${QM_VK_DIR}/sync/event_manager.hpp ${QM_VK_DIR}/sync/event_manager.cpp
	${QM_VK_DIR}/sync/fence.hpp ${QM_VK_DIR}/sync/fence.cpp
	${QM_VK_DIR}/sync/fence_manager.hpp ${QM_VK_DIR}/sync/fence_manager.cpp
	${QM_VK_DIR}/sync/pipeline_event.hpp ${QM_VK_DIR}/sync/pipeline_event.cpp
	${QM_VK_DIR}/sync/semaphore.hpp ${QM_VK_DIR}/sync/semaphore.cpp
	${QM_VK_DIR}/sync/semaphore_manager.hpp ${QM_VK_DIR}/sync/semaphore_manager.cpp
	
	${QM_VK_DIR}/wsi/wsi.hpp ${QM_VK_DIR}/wsi/wsi.cpp
	${QM_VK_DIR}/wsi/wsi_timing.hpp ${QM_VK_DIR}/wsi/wsi_timing.cpp
	
	${QM_VK_DIR}/context.hpp ${QM_VK_DIR}/context.cpp
	${QM_VK_DIR}/command_buffer.hpp ${QM_VK_DIR}/command_buffer.cpp
	${QM_VK_DIR}/command_pool.hpp ${QM_VK_DIR}/command_pool.cpp
	${QM_VK_DIR}/device.hpp ${QM_VK_DIR}/device.cpp
	${QM_VK_DIR}/device_shaders.cpp ${QM_VK_DIR}/device_submission.cpp
	${QM_VK_DIR}/vulkan_common.hpp
	${QM_VK_DIR}/vulkan_headers.hpp
	
	src/quantumvk.hpp)
	
if (QM_VULKAN_MT)
    target_compile_definitions(QuantumVk PUBLIC QM_VULKAN_MT)
endif()

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(QuantumVk PUBLIC VULKAN_DEBUG)
endif()

if (WIN32)
	target_compile_definitions(QuantumVk PUBLIC QM_PLATFORM_WINDOWS)
	target_compile_definitions(QuantumVk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()
	
target_compile_options(QuantumVk PRIVATE ${QM_CXX_FLAGS})
# --------------------------------

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests/example)

