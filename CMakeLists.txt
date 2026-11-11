cmake_minimum_required(VERSION 3.24)

project(SECMotionModelGUI
	VERSION 0.2.0
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
option(SECGUI_BUILD_WINDOW "Build the interactive GLFW + Dear ImGui window" ON)
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
	URL https://github.com/eliemichel/WebGPU-distribution/archive/17dcd42a7683355e7a40ac4e97e77f36dff5b5ab.tar.gz
	URL_HASH SHA256=e20d22d276b2be22938601a89992b32f2694cc03b6148af5618b2d256c715522
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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
	URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
	stb
	URL https://github.com/nothings/stb/archive/4e9b368bfc071e97206ea5c6adba68dd0cbf8897.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(stb)
if(NOT TARGET stb)
	add_library(stb INTERFACE)
	target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Interactive window: GLFW for the native window/surface and Dear ImGui for
# the HUD, timeline and panels. Both are compiled statically into the binary.
# ---------------------------------------------------------------------------
if(SECGUI_BUILD_WINDOW AND UNIX AND NOT APPLE)
	find_package(X11 QUIET)
	if(NOT X11_Xrandr_INCLUDE_PATH OR NOT X11_Xinerama_INCLUDE_PATH
	   OR NOT X11_Xcursor_INCLUDE_PATH OR NOT X11_Xi_INCLUDE_PATH)
		message(WARNING
			"X11 development headers not found: building without the interactive window. "
			"Install xorg-dev (Debian/Ubuntu) or libXrandr-devel libXcursor-devel "
			"libXinerama-devel libXi-devel (Fedora), then re-run CMake.")
		set(SECGUI_BUILD_WINDOW OFF)
	else()
		message(STATUS "Interactive window: GLFW + Dear ImGui")
	endif()
endif()

if(SECGUI_BUILD_WINDOW)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

	FetchContent_Declare(
		glfw
		URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
		URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(glfw)

	FetchContent_Declare(
		glfw3webgpu
		URL https://github.com/eliemichel/glfw3webgpu/archive/8f14534e79c590e8a7f7795ea42e01bedea9e137.tar.gz
		URL_HASH SHA256=38a2623803f8c19e2bab46518e964b07086b476ae894dd0e79b936b7c14324a3
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(glfw3webgpu)

	FetchContent_Declare(
		imgui
		URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
		URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(imgui)
	if(NOT TARGET secgui_imgui)
		add_library(secgui_imgui STATIC
			"${imgui_SOURCE_DIR}/imgui.cpp"
			"${imgui_SOURCE_DIR}/imgui_draw.cpp"
			"${imgui_SOURCE_DIR}/imgui_tables.cpp"
			"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
			"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
			"${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp"
		)
		target_include_directories(secgui_imgui PUBLIC
			"${imgui_SOURCE_DIR}"
			"${imgui_SOURCE_DIR}/backends"
		)
		target_compile_definitions(secgui_imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU GLFW_INCLUDE_NONE)
		target_link_libraries(secgui_imgui PUBLIC glfw webgpu)
	endif()
	if(TARGET glfw3webgpu)
		target_compile_definitions(glfw3webgpu PRIVATE GLFW_INCLUDE_NONE)
		if(WIN32)
			target_compile_definitions(glfw3webgpu PRIVATE _GLFW_WIN32)
		elseif(APPLE)
			target_compile_definitions(glfw3webgpu PRIVATE _GLFW_COCOA)
			target_compile_options(glfw3webgpu PRIVATE -x objective-c)
			target_link_libraries(glfw3webgpu PRIVATE
				"-framework Cocoa" "-framework CoreVideo" "-framework IOKit" "-framework QuartzCore")
		else()
			target_compile_definitions(glfw3webgpu PRIVATE _GLFW_X11)
			target_include_directories(glfw3webgpu PRIVATE
				"${X11_X11_INCLUDE_PATH}"
				"${X11_Xrandr_INCLUDE_PATH}"
				"${X11_Xinerama_INCLUDE_PATH}"
				"${X11_Xcursor_INCLUDE_PATH}"
				"${X11_Xi_INCLUDE_PATH}"
				"${X11_Xshape_INCLUDE_PATH}"
			)
		endif()
	endif()
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
	src/render/Camera.cpp
	src/scene/FrameData.cpp
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
if(SECGUI_BUILD_WINDOW)
	target_link_libraries(secmotion-gui PRIVATE secgui_imgui glfw3webgpu glfw)
	target_compile_definitions(secmotion-gui PRIVATE SECGUI_WINDOW=1)
endif()

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
