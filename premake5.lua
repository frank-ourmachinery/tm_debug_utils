-- premake5.lua
-- version: premake-5.0.0-alpha14

-- %TM_SDK_DIR% should be set to the directory of The Machinery SDK

newoption {
    trigger     = "clang",
    description = "Force use of CLANG for Windows builds"
}

-- Include all project files from specified folder
function folder(t)
    if type(t) ~= "table" then t = {t} end
    for _,f in ipairs(t) do
        files {f .. "/**.h",  f .. "/**.c", f .. "/**.inl", f .. "/**.cpp", f .. "/**.m", f .. "/**.shader"}
    end
end

workspace "debug-utils"
    configurations {"Debug", "Release"}
    language "C++"
    cppdialect "C++11"
    flags { "FatalWarnings", "MultiProcessorCompile" }
    warnings "Extra"
    inlining "Auto"
    sysincludedirs { "" }
    targetdir "bin/%{cfg.buildcfg}"

filter "system:windows"
    platforms { "Win64" }
    systemversion("latest")

filter { "system:windows", "options:clang" }
    toolset("msc-clangcl")
    buildoptions {
        "-Wno-missing-field-initializers",   -- = {0} is OK.
        "-Wno-unused-parameter",             -- Useful for documentation purposes.
        "-Wno-unused-local-typedef",         -- We don't always use all typedefs.
        "-Wno-missing-braces",               -- = {0} is OK.
        "-Wno-microsoft-anon-tag",           -- Allow anonymous structs.
    }
    buildoptions {
        "-fms-extensions",                   -- Allow anonymous struct as C inheritance.
        "-mavx",                             -- AVX.
        "-mfma",                             -- FMA.
    }
    removeflags {"FatalLinkWarnings"}        -- clang linker doesn't understand /WX

filter "platforms:Win64"
    defines { "TM_OS_WINDOWS", "_CRT_SECURE_NO_WARNINGS" }
    includedirs { "%TM_SDK_DIR%/headers" }
    staticruntime "On"
    architecture "x64"
    prebuildcommands {
        "if not defined TM_SDK_DIR (echo ERROR: Environment variable TM_SDK_DIR must be set)"
    }
    libdirs { "%TM_SDK_DIR%/lib/" .. _ACTION .. "/%{cfg.buildcfg}"}
    disablewarnings {
        "4057", -- Slightly different base types. Converting from type with volatile to without.
        "4100", -- Unused formal parameter. I think unusued parameters are good for documentation.
        "4152", -- Conversion from function pointer to void *. Should be ok.
        "4200", -- Zero-sized array. Valid C99.
        "4201", -- Nameless struct/union. Valid C11.
        "4204", -- Non-constant aggregate initializer. Valid C99.
        "4206", -- Translation unit is empty. Might be #ifdefed out.
        "4214", -- Bool bit-fields. Valid C99.
        "4221", -- Pointers to locals in initializers. Valid C99.
        "4702", -- Unreachable code. We sometimes want return after exit() because otherwise we get an error about no return value.
    }
    linkoptions {"/ignore:4099"}

filter "configurations:Debug"
    defines { "TM_CONFIGURATION_DEBUG", "DEBUG" }
    symbols "On"

filter "configurations:Release"
    defines { "TM_CONFIGURATION_RELEASE" }
    optimize "On"

project "debug-utils"
    location "build/debug_utils"
    targetname "tm_debug_utils"
    kind "SharedLib"
    language "C++"
	folder{ "shared", "tm_debug_utils" }
	includedirs { "shared", "tm_debug_utils", "$(TM_SDK_DIR)" }
	libdirs { "$(TM_SDK_DIR)/bin/%{cfg.buildcfg}" }
	links { "foundation.lib" }
	defines { "TM_LINKS_FOUNDATION", "TM_LINKS_DEBUG_UTILS" }
    sysincludedirs { "" }
    filter "platforms:Win64"
        targetdir "$(TM_SDK_DIR)/bin/%{cfg.buildcfg}/plugins"
		
project "debug-utils-static"
    location "build/debug_utils"
    targetname "tm_debug_utils_static"
    kind "StaticLib"
    language "C++"
	folder{ "shared", "tm_debug_utils" }
	includedirs { "shared", "tm_debug_utils", "$(TM_SDK_DIR)" }
	libdirs { "$(TM_SDK_DIR)/bin/%{cfg.buildcfg}" }
	links { "foundation.lib" }
	defines { "TM_LINKS_FOUNDATION", "TM_LINKS_DEBUG_UTILS" }
    sysincludedirs { "" }
    filter "platforms:Win64"
        targetdir "build/debug_utils_static/lib"
		
project "symbols"
    location "build/symbols"
    targetname "symbols"
    kind "ConsoleApp"
    language "C++"
	folder{ "symbols" }
	includedirs { "symbols", "shared", "tm_debug_utils", "$(TM_SDK_DIR)" }
	libdirs { "$(TM_SDK_DIR)/bin/%{cfg.buildcfg}", "build/debug_utils_static/lib" }
	links { "foundation.lib", "tm_debug_utils_static.lib" }
	defines { "TM_LINKS_FOUNDATION", "TM_LINKS_DEBUG_UTILS" }
    sysincludedirs { "" }
	dependson { "debug-utils-static" }
    filter "platforms:Win64"
        targetdir "$(TM_SDK_DIR)/bin/%{cfg.buildcfg}"
