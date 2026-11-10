#pragma once

#include <webgpu/webgpu.hpp>

namespace secgui {

// Minimal forward-looking renderer: Phase 1 draws a reference triangle so the
// whole pipeline (shader, render pass, screenshot readback) is exercised.
// Phase 3 replaces the body with the scene renderer.
class Renderer {
public:
	Renderer() = default;
	~Renderer() { release(); }

	void init(const wgpu::Device& device, wgpu::TextureFormat format);
	void render(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& view,
	            unsigned width, unsigned height);
	void release();

	wgpu::TextureFormat colorFormat() const { return colorFormat_; }

private:
	wgpu::RenderPipeline pipeline_;
	wgpu::ShaderModule shaderModule_;
	wgpu::TextureFormat colorFormat_ = wgpu::TextureFormat::Undefined;
};

} // namespace secgui
