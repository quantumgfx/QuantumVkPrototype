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

set_sln_folder(SPIRV-Tools-link "glslang/SPIRV-Tools")
set_sln_folder(SPIRV-Tools-opt "glslang/SPIRV-Tools")
set_sln_folder(SPIRV-Tools-reduce "glslang/SPIRV-Tools")
set_sln_folder(SPIRV-Tools-static "glslang/SPIRV-Tools")
set_sln_folder(SPIRV-Tools-shared "glslang/SPIRV-Tools")

set_sln_folder(spirv-tools-build-version "glslang/SPIRV-Tools/Utility")
set_sln_folder(spirv-tools-header-DebugInfo "glslang/SPIRV-Tools/Utility")
set_sln_folder(spirv-tools-header-OpenCLDebugInfo100 "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-cldi100 "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-clspvreflection "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-debuginfo "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-spv-amd-gs "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-spv-amd-sb "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-spv-amd-sevp "glslang/SPIRV-Tools/Utility")
set_sln_folder(spv-tools-spv-amd-stm "glslang/SPIRV-Tools/Utility")
set_sln_folder(spirv-tools-vimsyntax "glslang/SPIRV-Tools/Utility")
set_sln_folder(spirv-tools-pkg-config "glslang/SPIRV-Tools/Utility")
set_sln_folder(spirv-tools-shared-pkg-config "glslang/SPIRV-Tools/Utility")

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

set_sln_folder(GenericCodeGen "glslang")
set_sln_folder(glslang "glslang")
set_sln_folder(HLSL "glslang")
set_sln_folder(MachineIndependent "glslang")
set_sln_folder(OGLCompiler "glslang")
set_sln_folder(OSDependent "glslang")
set_sln_folder(SPIRV "glslang")

set_sln_folder(core_tables "glslang/Utility")
set_sln_folder(enum_string_mapping "glslang/Utility")
set_sln_folder(extinst_tables "glslang/Utility")
set_sln_folder(glslang-build-info "glslang/Utility")
# -------------------------------------------

