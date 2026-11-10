#pragma once

#include <webgpu/webgpu.hpp>

#include <string>

struct GLFWwindow;

namespace secgui {

// Owns the WebGPU instance/adapter/device/queue for the whole application.
struct GpuContext {
	wgpu::Instance instance;
	wgpu::Adapter adapter;
	wgpu::Device device;
	wgpu::Queue queue;
	wgpu::Surface surface;
	std::string adapterName;
	std::string backendName;

	GpuContext() = default;
	GpuContext(const GpuContext&) = delete;
	GpuContext& operator=(const GpuContext&) = delete;
	GpuContext(GpuContext&& other) noexcept;
	GpuContext& operator=(GpuContext&& other) noexcept;
	~GpuContext();

	// Throws std::runtime_error when no adapter/device can be created.
	static GpuContext create(const std::string& backendPreference);

#ifdef SECGUI_WINDOW
	// Creates the instance and a surface for an existing GLFW window, then
	// requests an adapter compatible with that surface.
	static GpuContext createForWindow(const std::string& backendPreference, GLFWwindow* window);
#endif

	// Processes pending instance events and device callbacks.
	void pump();

private:
	static GpuContext createImpl(const std::string& backendPreference, GLFWwindow* window);
	void release();
};

} // namespace secgui
