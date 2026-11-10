#include "gpu/GpuContext.hpp"

#include "util/Log.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace secgui {

namespace {

wgpu::BackendType parseBackend(const std::string& name) {
	if (name.empty() || name == "auto") return wgpu::BackendType::Undefined;
	if (name == "vulkan") return wgpu::BackendType::Vulkan;
	if (name == "d3d12") return wgpu::BackendType::D3D12;
	if (name == "d3d11") return wgpu::BackendType::D3D11;
	if (name == "metal") return wgpu::BackendType::Metal;
	if (name == "opengl" || name == "opengles" || name == "gles") return wgpu::BackendType::OpenGLES;
	if (name == "null") return wgpu::BackendType::Null;
	throw std::runtime_error("unknown backend: '" + name + "' (expected auto, vulkan, d3d12, d3d11, metal, opengl, null)");
}

const char* backendTypeName(WGPUBackendType type) {
	switch (type) {
	case WGPUBackendType_Undefined: return "auto";
	case WGPUBackendType_Null: return "null";
	case WGPUBackendType_WebGPU: return "webgpu";
	case WGPUBackendType_D3D11: return "d3d11";
	case WGPUBackendType_D3D12: return "d3d12";
	case WGPUBackendType_Metal: return "metal";
	case WGPUBackendType_Vulkan: return "vulkan";
	case WGPUBackendType_OpenGL: return "opengl";
	case WGPUBackendType_OpenGLES: return "opengles";
	default: return "unknown";
	}
}

} // namespace

GpuContext::GpuContext(GpuContext&& other) noexcept
	: instance(other.instance)
	, adapter(other.adapter)
	, device(other.device)
	, queue(other.queue)
	, adapterName(std::move(other.adapterName))
	, backendName(std::move(other.backendName)) {
	other.instance = wgpu::Instance();
	other.adapter = wgpu::Adapter();
	other.device = wgpu::Device();
	other.queue = wgpu::Queue();
}

GpuContext& GpuContext::operator=(GpuContext&& other) noexcept {
	if (this != &other) {
		release();
		instance = other.instance;
		adapter = other.adapter;
		device = other.device;
		queue = other.queue;
		adapterName = std::move(other.adapterName);
		backendName = std::move(other.backendName);
		other.instance = wgpu::Instance();
		other.adapter = wgpu::Adapter();
		other.device = wgpu::Device();
		other.queue = wgpu::Queue();
	}
	return *this;
}

GpuContext::~GpuContext() {
	release();
}

void GpuContext::release() {
	if (queue) queue.release();
	if (device) device.release();
	if (adapter) adapter.release();
	if (instance) instance.release();
	queue = wgpu::Queue();
	device = wgpu::Device();
	adapter = wgpu::Adapter();
	instance = wgpu::Instance();
}

void GpuContext::pump() {
	if (!instance) {
		return;
	}
	instance.processEvents();
	if (device) {
		device.poll(false, nullptr);
	}
}

GpuContext GpuContext::create(const std::string& backendPreference) {
	GpuContext context;

	context.instance = wgpu::createInstance();
	if (!context.instance) {
		throw std::runtime_error("failed to create the WebGPU instance");
	}

	struct AdapterState {
		bool done = false;
		wgpu::Adapter adapter;
		std::string error;
	};

	AdapterState adapterState;
	wgpu::RequestAdapterOptions options{};
	options.powerPreference = wgpu::PowerPreference::HighPerformance;
	options.forceFallbackAdapter = false;
	options.backendType = parseBackend(backendPreference);
	const bool specificBackend = options.backendType != wgpu::BackendType::Undefined;

	auto requestAdapter = [&]() -> bool {
		adapterState = AdapterState{};
		wgpu::RequestAdapterCallbackInfo callbackInfo{};
		callbackInfo.mode = wgpu::CallbackMode::AllowProcessEvents;
		callbackInfo.userdata1 = &adapterState;
		callbackInfo.userdata2 = nullptr;
		callbackInfo.callback = [](
			WGPURequestAdapterStatus status,
			WGPUAdapter adapter,
			WGPUStringView message,
			void* userdata1,
			void*) {
			auto* state = static_cast<AdapterState*>(userdata1);
			if (status == WGPURequestAdapterStatus_Success && adapter != nullptr) {
				state->adapter = wgpu::Adapter(adapter);
			} else {
				state->error = std::string(std::string_view(message));
			}
			state->done = true;
		};

		context.instance.requestAdapter(options, callbackInfo);
		while (!adapterState.done) {
			context.instance.processEvents();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return static_cast<bool>(adapterState.adapter);
	};

	if (!requestAdapter()) {
		if (specificBackend) {
			logWarn("no adapter for backend '" + backendPreference + "' (" + adapterState.error + "), retrying with auto");
			options.backendType = wgpu::BackendType::Undefined;
			if (!requestAdapter()) {
				throw std::runtime_error("no WebGPU adapter available: " + adapterState.error);
			}
		} else {
			throw std::runtime_error("no WebGPU adapter available: " + adapterState.error);
		}
	}

	context.adapter = adapterState.adapter;

	wgpu::AdapterInfo info{};
	if (context.adapter.getInfo(&info) == wgpu::Status::Success) {
		context.adapterName = std::string(std::string_view(info.device));
		if (context.adapterName.empty()) {
			context.adapterName = std::string(std::string_view(info.description));
		}
		context.backendName = backendTypeName(info.backendType);
	} else {
		context.adapterName = "unknown adapter";
		context.backendName = backendPreference;
	}

	struct DeviceState {
		bool done = false;
		wgpu::Device device;
		std::string error;
	};

	DeviceState deviceState;
	wgpu::DeviceDescriptor deviceDescriptor{};
	deviceDescriptor.label = wgpu::StringView("secmotion-gui device");
	deviceDescriptor.requiredFeatureCount = 0;
	deviceDescriptor.requiredFeatures = nullptr;
	deviceDescriptor.requiredLimits = nullptr;
	deviceDescriptor.defaultQueue.label = wgpu::StringView("secmotion-gui queue");
	deviceDescriptor.uncapturedErrorCallbackInfo.callback = [](
		WGPUDevice,
		WGPUErrorType type,
		WGPUStringView message,
		void*,
		void*) {
		logError(std::string("webgpu uncaptured error (type ")
			+ std::to_string(static_cast<int>(type)) + "): "
			+ std::string(std::string_view(message)));
	};
	deviceDescriptor.uncapturedErrorCallbackInfo.userdata1 = nullptr;
	deviceDescriptor.uncapturedErrorCallbackInfo.userdata2 = nullptr;

	wgpu::RequestDeviceCallbackInfo deviceCallback{};
	deviceCallback.mode = wgpu::CallbackMode::AllowProcessEvents;
	deviceCallback.userdata1 = &deviceState;
	deviceCallback.userdata2 = nullptr;
	deviceCallback.callback = [](
		WGPURequestDeviceStatus status,
		WGPUDevice device,
		WGPUStringView message,
		void* userdata1,
		void*) {
		auto* state = static_cast<DeviceState*>(userdata1);
		if (status == WGPURequestDeviceStatus_Success && device != nullptr) {
			state->device = wgpu::Device(device);
		} else {
			state->error = std::string(std::string_view(message));
		}
		state->done = true;
	};

	context.adapter.requestDevice(deviceDescriptor, deviceCallback);
	while (!deviceState.done) {
		context.instance.processEvents();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (!deviceState.device) {
		throw std::runtime_error("failed to create the WebGPU device: " + deviceState.error);
	}
	context.device = deviceState.device;
	context.queue = context.device.getQueue();

	logInfo("WebGPU ready: backend=" + context.backendName + " adapter=" + context.adapterName);
	return context;
}

} // namespace secgui
