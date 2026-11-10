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

struct RenderFrame {
	Mat4 viewProjection;
	Vec3 cameraRight{1.0, 0.0, 0.0};
	Vec3 cameraUp{0.0, 1.0, 0.0};
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

	// Per-frame dynamic data (ISL, dome, route, satellites).
	void update(const RenderFrame& frame);

	void render(const wgpu::CommandEncoder& encoder,
	            const wgpu::TextureView& colorView,
	            const wgpu::TextureView& depthView,
	            unsigned width, unsigned height);

	void release();

	wgpu::TextureFormat colorFormat() const { return colorFormat_; }
	std::size_t lastLineCount() const { return dynamicLineCount_; }
	std::size_t lastPointCount() const { return pointCount_; }

private:
	wgpu::Buffer createVertexBuffer(const char* label, std::size_t size) const;

	wgpu::Device device_;
	wgpu::Queue queue_;
	wgpu::TextureFormat colorFormat_ = wgpu::TextureFormat::Undefined;

	wgpu::BindGroupLayout uniformLayout_;
	wgpu::PipelineLayout pipelineLayout_;
	wgpu::Buffer uniformBuffer_;

	wgpu::ShaderModule lineModule_;
	wgpu::ShaderModule pointModule_;
	wgpu::RenderPipeline linePipeline_;
	wgpu::RenderPipeline pointPipeline_;
	wgpu::BindGroup uniformBindGroup_;

	std::vector<LineVertex> earthGrid_;
	wgpu::Buffer earthGridBuffer_;
	std::size_t earthGridCount_ = 0;

	wgpu::Buffer dynamicLineBuffer_;
	std::size_t dynamicLineCapacity_ = 0;
	std::size_t dynamicLineCount_ = 0;

	wgpu::Buffer pointBuffer_;
	std::size_t pointCapacity_ = 0;
	std::size_t pointCount_ = 0;
};

} // namespace secgui
