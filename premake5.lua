project "QuantumVk"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
	
	includedirs
	{
		"quantum"
	}

	files
	{
		"quantum/**.hpp",
		"quantum/**.cpp"
	}

	defines "VK_NO_PROTOTYPES"
	
	filter "system:windows"
		systemversion "latest"
		defines "VK_USE_PLATFORM_WIN32_KHR"
		defines "QM_PLATFORM_WINDOWS"

	filter "configurations:Debug"
		defines "QM_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		runtime "Release"
		optimize "on"
