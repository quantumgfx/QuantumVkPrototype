project(Example LANGUAGES CXX C)

cmake_policy(SET CMP0076 NEW)

add_library(GLFW STATIC)
target_sources(GLFW PUBLIC 
		"GLFW/include/GLFW/glfw3.h"
		"GLFW/include/GLFW/glfw3native.h"
		"GLFW/src/glfw_config.h"
		"GLFW/src/context.c"
		"GLFW/src/init.c"
		"GLFW/src/input.c"
		"GLFW/src/monitor.c"
		"GLFW/src/vulkan.c"
		"GLFW/src/window.c")
if(WIN32)
target_sources(GLFW PUBLIC 
			"GLFW/src/win32_init.c"
			"GLFW/src/win32_joystick.c"
			"GLFW/src/win32_monitor.c"
			"GLFW/src/win32_time.c"
			"GLFW/src/win32_thread.c"
			"GLFW/src/win32_window.c"
			"GLFW/src/wgl_context.c"
			"GLFW/src/egl_context.c"
			"GLFW/src/osmesa_context.c")
			
target_compile_definitions(GLFW PUBLIC "_GLFW_WIN32" "_CRT_SECURE_NO_WARNINGS")
endif()

if(UNIX)
target_sources(GLFW PUBLIC 
			"GLFW/src/x11_init.c"
			"GLFW/src/x11_monitor.c"
			"GLFW/src/x11_window.c"
			"GLFW/src/xkb_unicode.c"
			"GLFW/src/posix_time.c"
			"GLFW/src/posix_thread.c"
			"GLFW/src/glx_context.c"
			"GLFW/src/egl_context.c"
			"GLFW/src/osmesa_context.c"
			"GLFW/src/linux_joystick.c")
			
target_compile_definitions(GLFW PUBLIC _GLFW_X1)
endif()
		

add_executable(Example "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_link_libraries(Example PRIVATE QuantumVk)
target_link_libraries(Example PRIVATE GLFW)
target_include_directories(Example PUBLIC "${VULKAN_SDK_DIR}/Include")
target_include_directories(Example PUBLIC ${QM_VK_INCLUDE_DIR})
target_include_directories(Example PUBLIC "GLFW/include")