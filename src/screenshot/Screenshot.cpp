#include "screenshot/Screenshot.hpp"

#include "util/Log.hpp"
#include "util/Png.hpp"
#include "util/Sha256.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace secgui {

namespace {

std::size_t alignTo(std::size_t value, std::size_t alignment) {
	return ((value + alignment - 1) / alignment) * alignment;
}

} // namespace

ScreenshotResult ScreenshotService::capture(GpuContext& gpu, Renderer& renderer,
                                            unsigned width, unsigned height,
                                            const std::filesystem::path& path) {
	if (width == 0 || height == 0) {
		throw std::runtime_error("screenshot size must be positive");
	}

	const wgpu::TextureFormat format = renderer.colorFormat();

	wgpu::TextureDescriptor textureDescriptor{};
	textureDescriptor.label = wgpu::StringView("screenshot target");
	textureDescriptor.size = wgpu::Extent3D{width, height, 1};
	textureDescriptor.mipLevelCount = 1;
	textureDescriptor.sampleCount = 1;
	textureDescriptor.dimension = wgpu::TextureDimension::_2D;
	textureDescriptor.format = format;
	textureDescriptor.usage = static_cast<WGPUTextureUsage>(
		WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);
	const wgpu::Texture texture = gpu.device.createTexture(textureDescriptor);
	if (!texture) {
		throw std::runtime_error("failed to create the screenshot texture");
	}
	const wgpu::TextureView view = texture.createView();

	const std::size_t bytesPerRow = alignTo(static_cast<std::size_t>(width) * 4u, 256u);
	const std::size_t bufferSize = bytesPerRow * static_cast<std::size_t>(height);

	wgpu::BufferDescriptor bufferDescriptor{};
	bufferDescriptor.label = wgpu::StringView("screenshot readback");
	bufferDescriptor.size = bufferSize;
	bufferDescriptor.usage = static_cast<WGPUBufferUsage>(
		WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
	bufferDescriptor.mappedAtCreation = false;
	const wgpu::Buffer readback = gpu.device.createBuffer(bufferDescriptor);
	if (!readback) {
		throw std::runtime_error("failed to create the screenshot readback buffer");
	}

	const wgpu::CommandEncoder encoder = gpu.device.createCommandEncoder();

	renderer.render(encoder, view, width, height);

	wgpu::TexelCopyTextureInfo source{};
	source.texture = texture;
	source.mipLevel = 0;
	source.origin = wgpu::Origin3D{};
	source.aspect = wgpu::TextureAspect::All;

	wgpu::TexelCopyBufferInfo destination{};
	destination.buffer = readback;
	destination.layout.offset = 0;
	destination.layout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
	destination.layout.rowsPerImage = static_cast<uint32_t>(height);

	const wgpu::Extent3D copySize{width, height, 1};
	encoder.copyTextureToBuffer(source, destination, copySize);

	const wgpu::CommandBuffer commands = encoder.finish();
	gpu.queue.submit(commands);

	struct MapState {
		bool done = false;
		bool success = false;
		std::string error;
	};
	MapState mapState;
	wgpu::BufferMapCallbackInfo mapCallback{};
	mapCallback.mode = wgpu::CallbackMode::AllowProcessEvents;
	mapCallback.userdata1 = &mapState;
	mapCallback.userdata2 = nullptr;
	mapCallback.callback = [](
		WGPUMapAsyncStatus status,
		WGPUStringView message,
		void* userdata1,
		void*) {
		auto* state = static_cast<MapState*>(userdata1);
		state->success = (status == WGPUMapAsyncStatus_Success);
		state->error = std::string(std::string_view(message));
		state->done = true;
	};
	readback.mapAsync(wgpu::MapMode::Read, 0, bufferSize, mapCallback);

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (!mapState.done) {
		gpu.pump();
		if (std::chrono::steady_clock::now() > deadline) {
			throw std::runtime_error("timed out while waiting for the screenshot readback");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (!mapState.success) {
		throw std::runtime_error("failed to map the screenshot buffer: " + mapState.error);
	}

	const void* mapped = readback.getMappedRange(0, bufferSize);
	if (mapped == nullptr) {
		throw std::runtime_error("screenshot buffer mapped range is null");
	}
	const auto* mappedBytes = static_cast<const uint8_t*>(mapped);

	std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
	for (unsigned row = 0; row < height; ++row) {
		const uint8_t* sourceRow = mappedBytes + static_cast<std::size_t>(row) * bytesPerRow;
		uint8_t* destinationRow = pixels.data() + static_cast<std::size_t>(row) * width * 4u;
		std::copy(sourceRow, sourceRow + static_cast<std::size_t>(width) * 4u, destinationRow);
	}
	readback.unmap();

	ScreenshotResult result;
	result.width = width;
	result.height = height;
	result.bytes = pixels.size();
	result.sha256 = sha256Hex(pixels);

	double sum = 0.0;
	double sumSquares = 0.0;
	std::size_t samples = 0;
	uint8_t minimum = 255;
	uint8_t maximum = 0;
	for (std::size_t index = 0; index < pixels.size(); index += 4) {
		const double luminance = 0.2126 * pixels[index] + 0.7152 * pixels[index + 1] + 0.0722 * pixels[index + 2];
		sum += luminance;
		sumSquares += luminance * luminance;
		minimum = std::min(minimum, static_cast<uint8_t>(luminance));
		maximum = std::max(maximum, static_cast<uint8_t>(luminance));
		++samples;
	}
	result.stats.mean = samples > 0 ? sum / static_cast<double>(samples) : 0.0;
	const double variance = samples > 0
		? (sumSquares / static_cast<double>(samples)) - result.stats.mean * result.stats.mean
		: 0.0;
	result.stats.stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
	result.stats.nonblank = maximum > minimum && result.stats.stddev > 0.5;

	std::filesystem::create_directories(path.parent_path().empty()
		? std::filesystem::path(".")
		: path.parent_path());
	result.path = std::filesystem::absolute(path);
	if (!writePng(result.path, static_cast<int>(width), static_cast<int>(height), 4, pixels)) {
		throw std::runtime_error("failed to write the screenshot: " + result.path.string());
	}

	logInfo("screenshot written: " + result.path.string()
	        + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");

	commands.release();
	view.release();
	readback.release();
	texture.release();
	return result;
}

} // namespace secgui
