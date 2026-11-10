cmake_minimum_required(VERSION 3.24)

project(SECMotionModelGUI
	VERSION 0.1.0
	DESCRIPTION "Native WebGPU visualizer for the SECMotionModel satellite edge-computing simulator"
	LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SECGUI_STATIC "Prefer static linking of the WebGPU runtime and of the C++ runtime" ON)
option(SECGUI_BUILD_TESTS "Build tests" ON)
option(SECGUI_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# WebGPU: WebGPU-distribution provides both the WebGPU-Cpp headers and a
# pinned wgpu-native backend. The STATIC link type is required by the
# packaging requirements (self-contained .exe, AppImage, .dmg).
# ---------------------------------------------------------------------------
set(WEBGPU_BACKEND WGPU CACHE STRING "" FORCE)
set(WEBGPU_LINK_TYPE STATIC CACHE STRING "" FORCE)
set(WEBGPU_BUILD_FROM_SOURCE OFF CACHE BOOL "" FORCE)
set(WGPU_VERSION "v24.0.3.1" CACHE STRING "" FORCE)

FetchContent_Declare(
	webgpu_distribution
	GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
	GIT_TAG        17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
)
FetchContent_MakeAvailable(webgpu_distribution)

# The distribution declares MSVC-style import libraries for Windows. Replace
# them with MinGW equivalents when cross-compiling with mingw-w64.
if(MINGW)
	set_property(TARGET webgpu PROPERTY INTERFACE_LINK_LIBRARIES
		d3dcompiler
		ws2_32
		userenv
		ntdll
		bcrypt
		opengl32
		propsys
		runtimeobject
	)
endif()

# ---------------------------------------------------------------------------
# Other dependencies
# ---------------------------------------------------------------------------
FetchContent_Declare(
	nlohmann_json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
	stb
	GIT_REPOSITORY https://github.com/nothings/stb.git
	GIT_TAG        4e9b368bfc071e97206ea5c6adba68dd0cbf8897
)
FetchContent_MakeAvailable(stb)
if(NOT TARGET stb)
	add_library(stb INTERFACE)
	target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
set(SECGUI_SOURCES
	src/main.cpp
	src/app/Application.cpp
	src/cli/Args.cpp
	src/cli/JsonlDriver.cpp
	src/commands/CommandBus.cpp
	src/gpu/GpuContext.cpp
	src/gpu/WebGpuCpp.cpp
	src/render/Renderer.cpp
	src/scene/SceneLoader.cpp
	src/screenshot/Screenshot.cpp
	src/util/Log.cpp
	src/util/Png.cpp
	src/util/Sha256.cpp
)

add_executable(secmotion-gui ${SECGUI_SOURCES})

target_include_directories(secmotion-gui PRIVATE src)
target_link_libraries(secmotion-gui PRIVATE
	webgpu
	nlohmann_json::nlohmann_json
	stb
)

target_compile_definitions(secmotion-gui PRIVATE
	SECGUI_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
	target_compile_options(secmotion-gui PRIVATE /W4)
	if(SECGUI_WARNINGS_AS_ERRORS)
		target_compile_options(secmotion-gui PRIVATE /WX)
	endif()
else()
	target_compile_options(secmotion-gui PRIVATE -Wall -Wextra -Wpedantic)
	if(SECGUI_WARNINGS_AS_ERRORS)
		target_compile_options(secmotion-gui PRIVATE -Werror)
	endif()
endif()

if(MINGW AND SECGUI_STATIC)
	target_link_options(secmotion-gui PRIVATE
		-static
		-static-libgcc
		-static-libstdc++
		-Wl,--gc-sections
	)
endif()

if(APPLE)
	set_target_properties(secmotion-gui PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_GUI_IDENTIFIER "dev.secmotionmodel.gui"
		MACOSX_BUNDLE_BUNDLE_NAME "SECMotionModel GUI"
		MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
	)
endif()

install(TARGETS secmotion-gui
	RUNTIME DESTINATION bin
	BUNDLE  DESTINATION .
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SECGUI_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests/unit)
endif()
