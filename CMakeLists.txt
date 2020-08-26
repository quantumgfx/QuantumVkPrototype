cmake_minimum_required(VERSION 3.15)

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0077 NEW)

project(QuantumVk LANGUAGES CXX C)

# MultithreadedDebug if in debug mode, Mutlithreaded otherwise
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)

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

# Get Default vulkan sdk directory
set(DEFAULT_VULKAN_SDK_DIR "$ENV{VULKAN_SDK}")

# Options
option(QM_VULKAN_MT "Make QuantumVk thread-safe." ON)
option(QM_INSTALL "Run QunatumVk installation." ON)
# option(ENABLE_GLSL_TO_SPIRV_RUNTIME_CONVERSION "Allow shader modules to be created directly from glsl code" ON)

set(VULKAN_SDK_DIR "$ENV{VULKAN_SDK}")
# ------------------------------

add_subdirectory(extern EXCLUDE_FROM_ALL)
# -------------------------------

# QuantumVk Library
add_library(QuantumVk STATIC)
# Include Directories
target_include_directories(QuantumVk PUBLIC  ${VULKAN_SDK_DIR}/Include)
target_include_directories(QuantumVk PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR})

# libraries that QuantumVk links to
target_link_libraries(QuantumVk PRIVATE spirv-cross-core)

if (NOT WIN32)
    target_link_libraries(QuantumVk PRIVATE dl)
endif()

# Src Directories
set(QM_UTILS_DIR quantumvk/utils)
set(QM_VK_DIR quantumvk/vulkan)
set(QM_THREADING_DIR quantumvk/threading)
set(QM_EXTERN_BUILD_DIR quantumvk/extern_build)

set(QM_UTIL_HPP_FILES
		${QM_UTILS_DIR}/aligned_alloc.hpp
		${QM_UTILS_DIR}/bitops.hpp
		${QM_UTILS_DIR}/compile_time_hash.hpp
		${QM_UTILS_DIR}/enum_cast.hpp
		${QM_UTILS_DIR}/hash.hpp
		${QM_UTILS_DIR}/hashmap.hpp
		${QM_UTILS_DIR}/retained_alloc.hpp
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
		${QM_UTILS_DIR}/string_helpers.hpp
		${QM_UTILS_DIR}/temporary_hashmap.hpp
		${QM_UTILS_DIR}/timer.hpp
		${QM_UTILS_DIR}/variant.hpp)
		
set(QM_THREADING_HPP_FILES
		${QM_THREADING_DIR}/thread_group.hpp
		${QM_THREADING_DIR}/thread_id.hpp)
		
set(QM_VK_GRAPHICS_HPP_FILES
		${QM_VK_DIR}/graphics/descriptor_set.hpp
		${QM_VK_DIR}/graphics/render_pass.hpp 
		${QM_VK_DIR}/graphics/shader.hpp)
		
set(QM_VK_IMAGES_HPP_FILES
		${QM_VK_DIR}/images/format.hpp
		${QM_VK_DIR}/images/image.hpp 
		${QM_VK_DIR}/images/sampler.hpp 
		${QM_VK_DIR}/images/texture_format.hpp)

set(QM_VK_MEMORY_HPP_FILES
		${QM_VK_DIR}/memory/buffer.hpp 
		${QM_VK_DIR}/memory/buffer_pool.hpp 
		${QM_VK_DIR}/memory/memory_allocator.hpp)

set(QM_VK_MISC_HPP_FILES
		${QM_VK_DIR}/misc/cookie.hpp 
		${QM_VK_DIR}/misc/limits.hpp
		${QM_VK_DIR}/misc/quirks.hpp
		${QM_VK_DIR}/misc/type_to_string.hpp)
		
set(QM_VK_SYNC_HPP_FILES
		${QM_VK_DIR}/sync/event_manager.hpp 
		${QM_VK_DIR}/sync/fence.hpp 
		${QM_VK_DIR}/sync/fence_manager.hpp 
		${QM_VK_DIR}/sync/pipeline_event.hpp 
		${QM_VK_DIR}/sync/semaphore.hpp 
		${QM_VK_DIR}/sync/semaphore_manager.hpp)
		
set(QM_VK_WSI_HPP_FILES
		${QM_VK_DIR}/wsi/wsi.hpp 
		${QM_VK_DIR}/wsi/wsi_timing.hpp)

set(QM_VK_GLOBAL_FILES
		${QM_VK_DIR}/context.hpp 
		${QM_VK_DIR}/command_buffer.hpp 
		${QM_VK_DIR}/command_pool.hpp
		${QM_VK_DIR}/device.hpp 
		${QM_VK_DIR}/vulkan_common.hpp
		${QM_VK_DIR}/vulkan_headers.hpp)
		
		
set(QM_CPP_FILES 
		${QM_UTILS_DIR}/aligned_alloc.cpp
		${QM_UTILS_DIR}/string_helpers.cpp
		${QM_UTILS_DIR}/timer.cpp
		
		${QM_THREADING_DIR}/thread_group.cpp
		${QM_THREADING_DIR}/thread_id.cpp
		
		${QM_VK_DIR}/graphics/descriptor_set.cpp
		${QM_VK_DIR}/graphics/render_pass.cpp
		${QM_VK_DIR}/graphics/shader.cpp
		
		
		${QM_VK_DIR}/images/image.cpp
		${QM_VK_DIR}/images/sampler.cpp
		${QM_VK_DIR}/images/texture_format.cpp
		
		${QM_VK_DIR}/memory/buffer.cpp
		${QM_VK_DIR}/memory/buffer_pool.cpp
		${QM_VK_DIR}/memory/memory_allocator.cpp
		
		${QM_VK_DIR}/misc/cookie.cpp
		
		${QM_VK_DIR}/sync/event_manager.cpp
		${QM_VK_DIR}/sync/fence.cpp
		${QM_VK_DIR}/sync/fence_manager.cpp
		${QM_VK_DIR}/sync/pipeline_event.cpp
		${QM_VK_DIR}/sync/semaphore.cpp
		${QM_VK_DIR}/sync/semaphore_manager.cpp
		
		${QM_VK_DIR}/wsi/wsi.cpp
		${QM_VK_DIR}/wsi/wsi_timing.cpp
		
		${QM_VK_DIR}/context.cpp
		${QM_VK_DIR}/command_buffer.cpp
		${QM_VK_DIR}/command_pool.cpp
		${QM_VK_DIR}/device.cpp
		${QM_VK_DIR}/device_shaders.cpp 
		${QM_VK_DIR}/device_submission.cpp,
		
		${QM_EXTERN_BUILD_DIR}/vma_build.cpp,
		${QM_EXTERN_BUILD_DIR}/volk_build.cpp)

target_sources(QuantumVk PRIVATE ${QM_UTIL_HPP_FILES} ${QM_THREADING_HPP_FILES} ${QM_VK_GRAPHICS_HPP_FILES} ${QM_VK_SYNC_HPP_FILES}
								${QM_VK_IMAGES_HPP_FILES} ${QM_VK_MEMORY_HPP_FILES} ${QM_VK_MISC_HPP_FILES} ${QM_VK_WSI_HPP_FILES} ${QM_VK_GLOBAL_FILES}
								${QM_CPP_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/quantumvk/quantumvk.hpp)
	
if (QM_VULKAN_MT)
    target_compile_definitions(QuantumVk PUBLIC QM_VULKAN_MT)
endif()

if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    target_compile_definitions(QuantumVk PUBLIC VULKAN_DEBUG)
endif()

if (WIN32)
	target_compile_definitions(QuantumVk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()
	
target_compile_options(QuantumVk PRIVATE ${QM_CXX_FLAGS})

# ---- Installing -----
if(QM_INSTALL)

	include(GNUInstallDirs)
	
	set(QM_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
	set(QM_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
	
	install(TARGETS QuantumVk DESTINATION ${QM_INSTALL_LIBDIR})
	
	install(FILES ${QM_UTIL_HPP_FILES}        DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/utils)
	install(FILES ${QM_THREADING_HPP_FILES}   DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/threading)
	install(FILES ${QM_VK_GLOBAL_FILES}       DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan)
	install(FILES ${QM_VK_GRAPHICS_HPP_FILES} DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/graphics)
	install(FILES ${QM_VK_SYNC_HPP_FILES}     DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/sync)
	install(FILES ${QM_VK_IMAGES_HPP_FILES}   DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/images)
	install(FILES ${QM_VK_MEMORY_HPP_FILES}   DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/memory)
	install(FILES ${QM_VK_MISC_HPP_FILES}     DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/misc)
	install(FILES ${QM_VK_WSI_HPP_FILES}      DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/wsi)
	
	set(EXTERN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern)
	
	# External files that are included in headers
	install(FILES ${EXTERN_DIR}/volk/volk.h DESTINATION ${QM_INSTALL_INCLUDEDIR}/extern/volk)
	install(FILES ${EXTERN_DIR}/VulkanMemoryAllocator/vk_mem_alloc.h DESTINATION ${QM_INSTALL_INCLUDEDIR}/extern/VulkanMemoryAllocator)
	
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/quantumvk/quantumvk.hpp DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk)
	
endif()
