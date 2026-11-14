#pragma once

#include "render/Camera.hpp"

#include <webgpu/webgpu.hpp>

#include <cstddef>
#include <vector>

namespace secgui {

struct LineVertex {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

struct PointInstance {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
	float sizeKm = 60.0f;
};

struct GlobeVertex {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float nx = 0.0f;
	float ny = 0.0f;
	float nz = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
};

struct RenderFrame {
	Mat4 viewProjection;
	Vec3 cameraRight{1.0, 0.0, 0.0};
	Vec3 cameraUp{0.0, 1.0, 0.0};
	Vec3 cameraEye{0.0, 0.0, 0.0};
	Vec3 sunDirection{0.50, 0.72, 0.47};
	double earthRadiusKm = 6371.0;
	bool globeVisible = false;
	bool atmosphereVisible = false;
	std::vector<LineVertex> lines;
	std::vector<PointInstance> points;
};

class Renderer {
public:
	Renderer() = default;
	~Renderer() { release(); }

	void init(const wgpu::Device& device, const wgpu::Queue& queue, wgpu::TextureFormat format);

	// Static Earth wireframe; uploaded once.
	void setEarthGrid(std::vector<LineVertex> vertices);

	// Per-frame dynamic data (globe, ISL, dome, route, satellites).
	void update(const RenderFrame& frame);

	void render(const wgpu::CommandEncoder& encoder,
	            const wgpu::TextureView& colorView,
	            const wgpu::TextureView& depthView,
	            unsigned width, unsigned height);

	void release();

	wgpu::TextureFormat colorFormat() const { return colorFormat_; }
	std::size_t lastLineCount() const { return dynamicLineCount_; }
	std::size_t lastPointCount() const { return pointCount_; }
	std::size_t lastGlobeTriangleCount() const { return globeTriangleCount_; }

private:
	wgpu::Buffer createBuffer(const char* label, std::size_t size, WGPUBufferUsage usage) const;
	wgpu::Buffer createVertexBuffer(const char* label, std::size_t size) const;
	wgpu::Texture createEarthTexture() const;
	wgpu::Sampler createEarthSampler() const;

	wgpu::Device device_;
	wgpu::Queue queue_;
	wgpu::TextureFormat colorFormat_ = wgpu::TextureFormat::Undefined;

	wgpu::BindGroupLayout uniformLayout_;
	wgpu::BindGroupLayout textureLayout_;
	wgpu::PipelineLayout pipelineLayout_;
	wgpu::Buffer uniformBuffer_;

	wgpu::ShaderModule sceneModule_;
	wgpu::RenderPipeline linePipeline_;
	wgpu::RenderPipeline pointPipeline_;
	wgpu::RenderPipeline globePipeline_;
	wgpu::RenderPipeline atmospherePipeline_;
	wgpu::BindGroup uniformBindGroup_;

	wgpu::Texture earthTexture_;
	wgpu::TextureView earthTextureView_;
	wgpu::Sampler earthSampler_;
	wgpu::BindGroup earthBindGroup_;

	wgpu::Buffer globeVertexBuffer_;
	wgpu::Buffer globeIndexBuffer_;
	std::size_t globeVertexCount_ = 0;
	std::size_t globeIndexCount_ = 0;
	std::size_t globeTriangleCount_ = 0;

	std::vector<LineVertex> earthGrid_;
	wgpu::Buffer earthGridBuffer_;
	std::size_t earthGridCount_ = 0;

	wgpu::Buffer dynamicLineBuffer_;
	std::size_t dynamicLineCapacity_ = 0;
	std::size_t dynamicLineCount_ = 0;

	wgpu::Buffer pointBuffer_;
	std::size_t pointCapacity_ = 0;
	std::size_t pointCount_ = 0;

	bool globeVisible_ = false;
	bool atmosphereVisible_ = false;
	double earthRadiusKm_ = 6371.0;
};

} // namespace secgui
