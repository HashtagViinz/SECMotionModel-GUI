#pragma once

#include <webgpu/webgpu.hpp>

#include <string>

namespace secgui {

// Owns the WebGPU instance/adapter/device/queue for the whole application.
struct GpuContext {
	wgpu::Instance instance;
	wgpu::Adapter adapter;
	wgpu::Device device;
	wgpu::Queue queue;
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

	// Processes pending instance events and device callbacks.
	void pump();

private:
	void release();
};

} // namespace secgui
