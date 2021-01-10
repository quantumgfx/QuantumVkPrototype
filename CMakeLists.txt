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


# global project stuff
# put cmake build stuff in there own groups
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_C_STANDARD 99)

# We are overriding settings in subdirectories.
# Avoid warnings.

# Options
option(QM_VULKAN_MT "Make QuantumVk thread-safe." ON)

# ------------------------------


add_subdirectory(extern/Vulkan-Headers)

# Use FindVulkan module added with CMAKE 3.7
# message(STATUS "Using module to find Vulkan")
# find_package(Vulkan)

# QuantumVk Library
add_library(QuantumVk STATIC)

target_link_libraries(QuantumVk PUBLIC Vulkan-Headers)
target_include_directories(QuantumVk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/VulkanMemoryAllocator/src)


set(QM_SRC_DIR src/quantumvk)

set(QM_GLOBAL_HPP_FILES 
	${QM_SRC_DIR}/quantumvk.hpp)
	
set(QM_BASE_HPP_FILES 
	${QM_SRC_DIR}/base/vk.hpp
	${QM_SRC_DIR}/base/vma.hpp)

set(QM_CORE_HPP_FILES 
	${QM_SRC_DIR}/core/loader.hpp
	${QM_SRC_DIR}/core/instance.hpp
	${QM_SRC_DIR}/core/physical_device.hpp
	${QM_SRC_DIR}/core/device.hpp
	${QM_SRC_DIR}/core/queue.hpp
	${QM_SRC_DIR}/core/commands.hpp
	${QM_SRC_DIR}/core/memory.hpp)

set(QM_CORE_CPP_FILES 
	${QM_SRC_DIR}/core/loader.cpp
	${QM_SRC_DIR}/core/instance.cpp
	${QM_SRC_DIR}/core/physical_device.cpp
	${QM_SRC_DIR}/core/device.cpp
	${QM_SRC_DIR}/core/queue.cpp
	${QM_SRC_DIR}/core/commands.cpp
	${QM_SRC_DIR}/core/memory.cpp)

set(QM_FACTORY_HPP_FILES
	${QM_SRC_DIR}/factory/debug_factory.hpp
	${QM_SRC_DIR}/factory/instance_factory.hpp
	${QM_SRC_DIR}/factory/physical_device_selector.hpp)

set(QM_FACTORY_CPP_FILES
	${QM_SRC_DIR}/factory/debug_factory.cpp
	${QM_SRC_DIR}/factory/instance_factory.cpp
	${QM_SRC_DIR}/factory/physical_device_selector.cpp)	


target_sources(QuantumVk PRIVATE ${QM_GLOBAL_HPP_FILES} ${QM_BASE_HPP_FILES} ${QM_CORE_HPP_FILES} ${QM_CORE_CPP_FILES} ${QM_FACTORY_HPP_FILES} ${QM_FACTORY_CPP_FILES})
	
if (QM_VULKAN_MT)
    target_compile_definitions(QuantumVk PUBLIC QM_VULKAN_MT)
endif()

target_compile_definitions(QuantumVk PUBLIC $<$<CONFIG:DEBUG>:VULKAN_DEBUG>)

if (WIN32)
	target_compile_definitions(QuantumVk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()
	
target_compile_options(QuantumVk PRIVATE ${QM_CXX_FLAGS})

# ---- Installing -----
#if(QM_INSTALL)

#	include(GNUInstallDirs)
	
#	set(QM_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
#	set(QM_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
	
#	install(TARGETS QuantumVk DESTINATION ${QM_INSTALL_LIBDIR})
	
#	install(FILES ${QM_UTIL_HPP_FILES}         DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/utils)
#	install(FILES ${QM_EXTERN_BUILD_HPP_FILES} DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/extern_build)
#	install(FILES ${QM_VK_GLOBAL_FILES}        DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan)
#	install(FILES ${QM_VK_GRAPHICS_HPP_FILES}  DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/graphics)
#	install(FILES ${QM_VK_SYNC_HPP_FILES}      DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/sync)
#	install(FILES ${QM_VK_IMAGES_HPP_FILES}    DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/images)
#	install(FILES ${QM_VK_MEMORY_HPP_FILES}    DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/memory)
#	install(FILES ${QM_VK_MISC_HPP_FILES}      DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/misc)
#	install(FILES ${QM_VK_WSI_HPP_FILES}       DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk/vulkan/wsi)
#	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/quantumvk/quantumvk.hpp DESTINATION ${QM_INSTALL_INCLUDEDIR}/quantumvk)
	
#	set(EXTERN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern)
	
	# External files that are included in headers
#	install(FILES ${EXTERN_DIR}/volk/volk.h DESTINATION ${QM_INSTALL_INCLUDEDIR}/extern/volk)
#	install(FILES ${EXTERN_DIR}/VulkanMemoryAllocator/vk_mem_alloc.h DESTINATION ${QM_INSTALL_INCLUDEDIR}/extern/VulkanMemoryAllocator)
	
#	set(SPIRV_CROSS_DIR ${EXTERN_DIR}/SPIRV-Cross)
	
#	install(FILES ${SPIRV_CROSS_DIR}/GLSL.std.450.h 
#				${SPIRV_CROSS_DIR}/spirv_common.hpp
#				${SPIRV_CROSS_DIR}/spirv_cross_containers.hpp
#				${SPIRV_CROSS_DIR}/spirv_cross_error_handling.hpp
#				${SPIRV_CROSS_DIR}/spirv.hpp
#				${SPIRV_CROSS_DIR}/spirv_cross.hpp
#				${SPIRV_CROSS_DIR}/spirv_parser.hpp
#				${SPIRV_CROSS_DIR}/spirv_cross_parsed_ir.hpp
#				${SPIRV_CROSS_DIR}/spirv_cfg.hpp
#				DESTINATION ${QM_INSTALL_INCLUDEDIR}/extern/SPIRV-Cross)
	
#endif()
