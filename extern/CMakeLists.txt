# Subdirectory to set and add other subdirectories :)

macro(override_option setting value)
	set(${setting} ${value} CACHE INTERNAL "" FORCE)
endmacro()

macro(exclude_target target)
	set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endmacro()

# ---------SPIRV-Cross-------------
override_option(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF)
override_option(SPIRV_CROSS_SHARED OFF)
override_option(SPIRV_CROSS_STATIC ON)
override_option(SPIRV_CROSS_CLI OFF)

override_option(SPIRV_CROSS_ENABLE_TESTS OFF)
override_option(SPIRV_CROSS_ENABLE_GLSL OFF)
override_option(SPIRV_CROSS_ENABLE_HLSL OFF)
override_option(SPIRV_CROSS_ENABLE_MSL OFF)
override_option(SPIRV_CROSS_ENABLE_CPP OFF)
override_option(SPIRV_CROSS_ENABLE_REFLECT OFF)
override_option(SPIRV_CROSS_ENABLE_C_API OFF)
override_option(SPIRV_CROSS_ENABLE_UTIL OFF)

override_option(SPIRV_CROSS_SANITIZE_ADDRESS OFF)
override_option(SPIRV_CROSS_SANITIZE_MEMORY OFF)
override_option(SPIRV_CROSS_SANITIZE_THREADS OFF)
override_option(SPIRV_CROSS_SANITIZE_UNDEFINED OFF)

override_option(SPIRV_CROSS_NAMESPACE_OVERRIDE "")
override_option(SPIRV_CROSS_FORCE_STL_TYPES OFF)

override_option(SPIRV_CROSS_SKIP_INSTALL ON)

override_option(SPIRV_CROSS_WERROR OFF)
override_option(SPIRV_CROSS_MISC_WARNINGS OFF)
override_option(SPIRV_CROSS_FORCE_PIC OFF)

# Sets to not found. These are only used if testing is enabled, which it isn't.
override_option(spirv-cross-glslang "NOTFOUND")
override_option(spirv-cross-spirv-as "NOTFOUND")
override_option(spirv-cross-spirv-val "NOTFOUND")
override_option(spirv-cross-spirv-opt "NOTFOUND")

add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)

set_sln_folder(spirv-cross-core Dependencies)
# -------------------------------------------
# -------------SPIRV-Headers-----------------
override_option(SPIRV_HEADERS_SKIP_EXAMPLES ON)
override_option(SPIRV_HEADERS_SKIP_INSTALL ON)

#SPIRV HEADERS added via shaderc
# add_subdirectory(SPIRV-Headers EXCLUDE_FROM_ALL)
# -------------------------------------------
# ------------SPIRV-Tools--------------------

override_option(SPIRV_ALLOW_TIMERS ON)
override_option(SKIP_SPIRV_TOOLS_INSTALL ON)
override_option(SPIRV_BUILD_COMPRESSION OFF)
override_option(SPIRV_WERROR ON)
override_option(SPIRV_WARN_EVERYTHING OFF)
override_option(SPIRV_COLOR_TERMINAL ON)
override_option(SPIRV_LOG_DEBUG OFF)
override_option(SPIRV_SKIP_EXECUTABLES ON)
override_option(SPIRV_SKIP_TESTS ON)
override_option(SPIRV_CHECK_CONTEXT ON)
override_option(SPIRV_TOOLS_INSTALL_EMACS_HELPERS OFF)

set(SPIRV-Headers_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Headers)

add_subdirectory(SPIRV-Tools EXCLUDE_FROM_ALL)

# -------------------------------------------
# --------------glslang----------------------
override_option(BUILD_SHARED_LIBS OFF)
override_option(BUILD_EXTERNAL OFF)
override_option(SKIP_GLSLANG_INSTALL ON)
override_option(ENABLE_SPVREMAPPER OFF)
override_option(ENABLE_GLSLANG_BINARIES OFF)
override_option(ENABLE_GLSLANG_JS OFF)
override_option(ENABLE_RTTI OFF)
override_option(ENABLE_EXCEPTIONS OFF)
override_option(ENABLE_OPT ON)
override_option(ENABLE_PCH ON)
override_option(ENABLE_CTEST OFF)
override_option(USE_CCACHE OFF)
override_option(LLVM_USE_CRT_DEBUG MTd)
override_option(LLVM_USE_CRT_RELEASE MT)
override_option(LLVM_USE_CRT_MINSIZEREL MT)
override_option(LLVM_USE_CRT_RELWITHDEBINFO MT)
override_option(ENABLE_HLSL ON)

add_subdirectory(glslang EXCLUDE_FROM_ALL)

set_sln_folder(GenericCodeGen "Shaderc/glslang")
set_sln_folder(glslang "Shaderc/glslang")
set_sln_folder(HLSL "Shaderc/glslang")
set_sln_folder(MachineIndependent "Shaderc/glslang")
set_sln_folder(OGLCompiler "Shaderc/glslang")
set_sln_folder(OSDependent "Shaderc/glslang")
set_sln_folder(SPIRV "Shaderc/glslang")
# -------------------------------------------
# -------------shaderc-----------------------
override_option(SHADERC_ENABLE_SPVC OFF)
override_option(SHADERC_ENABLE_WGSL_OUTPUT OFF)
override_option(SHADERC_SKIP_INSTALL ON)
override_option(SHADERC_SKIP_TESTS ON)
override_option(SHADERC_SPVC_ENABLE_DIRECT_LOGGING OFF)
override_option(SHADERC_SPVC_DISABLE_CONTEXT_LOGGING OFF)
override_option(SHADERC_ENABLE_WERROR_COMPILE OFF)
override_option(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF)
override_option(SHADERC_ENABLE_SHARED_CRT OFF)

override_option(SHADERC_THIRD_PARTY_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

override_option(SHADERC_EFFCEE_DIR "Unnessasary")
override_option(SHADERC_RE2_DIR "Unnessasary")
override_option(SHADERC_TINT_DIR "Unnessasary")
override_option(SHADERC_GOOGLE_TEST_DIR "Unnessasary")
override_option(SHADERC_SPIRV_HEADERS_DIR "Unnessasary")
override_option(SHADERC_SPIRV_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Tools)
override_option(SHADERC_SPIRV_CROSS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Cross)
override_option(SHADERC_GLSLANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glslang)

override_option(ASCIIDOCTOR_EXE ASCIIDOCTOR_EXE-NOTFOUND)
override_option(DISABLE_EXCEPTIONS OFF)
override_option(DISABLE_RTTI OFF)
override_option(ENABLE_CODE_COVERAGE OFF)
override_option(gtest_forced_shared_crt OFF)

override_option(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF)

add_subdirectory(shaderc EXCLUDE_FROM_ALL)

set_sln_folder(shaderc "Shaderc")
set_sln_folder(shaderc_util "Shaderc")

set_sln_folder(add-copyright "Shaderc/Utility")
set_sln_folder(build-version "Shaderc/Utility")
set_sln_folder(check-copyright "Shaderc/Utility")
set_sln_folder(core_tables "Dependencies/Utility")
set_sln_folder(enum_string_mapping "Dependencies/Utility")
set_sln_folder(extinst_tables "Dependencies/Utility")
set_sln_folder(glslang-build-info "Shaderc/glslang/Utility")
set_sln_folder(shaderc_combined_genfile "Shaderc/Utility")
set_sln_folder(shaderc_combined-pkg-config "Shaderc/Utility")
set_sln_folder(shaderc_static-pkg-config "Shaderc/Utility")
set_sln_folder(shaderc-pkg-config "Shaderc/Utility")
set_sln_folder(spirv-tools-pkg-config "Dependencies/Utility")
set_sln_folder(spirv-tools-shared-pkg-config "Dependencies/Utility")
set_sln_folder(testdata "Shaderc/Utility")

#override_option(ASCIIDOCTOR_EXE "Unnessasary")
# -------------------------------------------

