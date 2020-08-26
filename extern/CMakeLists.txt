# Subdirectory to set and add other subdirectories :)
# Obsolete now. In order to build all of quantum vk into one static lib every subproject's headers and src files are referenced in quantumvk/extern_build
# macro(override_option setting value)
#	set(${setting} ${value} CACHE INTERNAL "" FORCE)
# endmacro()
# 
# macro(exclude_target target)
#	set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
# endmacro()

# ---------SPIRV-Cross-------------
#override_option(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF)
#override_option(SPIRV_CROSS_SHARED OFF)
#override_option(SPIRV_CROSS_STATIC ON)
#override_option(SPIRV_CROSS_CLI OFF)

#override_option(SPIRV_CROSS_ENABLE_TESTS OFF)
#override_option(SPIRV_CROSS_ENABLE_GLSL OFF)
#override_option(SPIRV_CROSS_ENABLE_HLSL OFF)
#override_option(SPIRV_CROSS_ENABLE_MSL OFF)
#override_option(SPIRV_CROSS_ENABLE_CPP OFF)
#override_option(SPIRV_CROSS_ENABLE_REFLECT OFF)
#override_option(SPIRV_CROSS_ENABLE_C_API OFF)
#override_option(SPIRV_CROSS_ENABLE_UTIL OFF)

#override_option(SPIRV_CROSS_SANITIZE_ADDRESS OFF)
#override_option(SPIRV_CROSS_SANITIZE_MEMORY OFF)
#override_option(SPIRV_CROSS_SANITIZE_THREADS OFF)
#override_option(SPIRV_CROSS_SANITIZE_UNDEFINED OFF)

#override_option(SPIRV_CROSS_NAMESPACE_OVERRIDE "")
#override_option(SPIRV_CROSS_FORCE_STL_TYPES OFF)

#override_option(SPIRV_CROSS_SKIP_INSTALL OFF)

#override_option(SPIRV_CROSS_WERROR OFF)
#override_option(SPIRV_CROSS_MISC_WARNINGS OFF)
#override_option(SPIRV_CROSS_FORCE_PIC OFF)

# Sets to not found. These are only used if testing is enabled, which it isn't.
#override_option(spirv-cross-glslang "NOTFOUND")
#override_option(spirv-cross-spirv-as "NOTFOUND")
#override_option(spirv-cross-spirv-val "NOTFOUND")
#override_option(spirv-cross-spirv-opt "NOTFOUND")

#add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)

#set_sln_folder(spirv-cross-core Dependencies)
# -------------------------------------------