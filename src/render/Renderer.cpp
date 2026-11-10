#include "render/Renderer.hpp"

#include "util/Log.hpp"

#include <stdexcept>

namespace secgui {

namespace {

constexpr const char* kReferenceShader = R"(
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4f {
	var positions = array<vec2f, 3>(
		vec2f( 0.0,  0.55),
		vec2f(-0.55, -0.45),
		vec2f( 0.55, -0.45)
	);
	return vec4f(positions[vertexIndex], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
	return vec4f(0.35, 0.85, 0.55, 1.0);
}
)";

} // namespace

void Renderer::init(const wgpu::Device& device, wgpu::TextureFormat format) {
	colorFormat_ = format;

	wgpu::ShaderSourceWGSL wgsl{};
	wgsl.setDefault();
	wgsl.code = wgpu::StringView(kReferenceShader);

	wgpu::ShaderModuleDescriptor moduleDescriptor{};
	moduleDescriptor.nextInChain = &wgsl.chain;
	moduleDescriptor.label = wgpu::StringView("reference shader");
	shaderModule_ = device.createShaderModule(moduleDescriptor);
	if (!shaderModule_) {
		throw std::runtime_error("failed to create the reference shader module");
	}

	wgpu::ColorTargetState target{};
	target.format = format;
	target.blend = nullptr;
	target.writeMask = WGPUColorWriteMask_All;

	wgpu::FragmentState fragment{};
	fragment.module = shaderModule_;
	fragment.entryPoint = wgpu::StringView("fs_main");
	fragment.constantCount = 0;
	fragment.constants = nullptr;
	fragment.targetCount = 1;
	fragment.targets = &target;

	wgpu::RenderPipelineDescriptor pipelineDescriptor{};
	pipelineDescriptor.label = wgpu::StringView("reference triangle pipeline");
	pipelineDescriptor.layout = nullptr;
	pipelineDescriptor.vertex.module = shaderModule_;
	pipelineDescriptor.vertex.entryPoint = wgpu::StringView("vs_main");
	pipelineDescriptor.vertex.constantCount = 0;
	pipelineDescriptor.vertex.constants = nullptr;
	pipelineDescriptor.vertex.bufferCount = 0;
	pipelineDescriptor.vertex.buffers = nullptr;
	pipelineDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pipelineDescriptor.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
	pipelineDescriptor.primitive.frontFace = wgpu::FrontFace::CCW;
	pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
	pipelineDescriptor.fragment = &fragment;
	pipelineDescriptor.multisample.count = 1;
	pipelineDescriptor.multisample.mask = 0xFFFFFFFFu;
	pipelineDescriptor.multisample.alphaToCoverageEnabled = false;
	pipelineDescriptor.depthStencil = nullptr;

	pipeline_ = device.createRenderPipeline(pipelineDescriptor);
	if (!pipeline_) {
		throw std::runtime_error("failed to create the reference render pipeline");
	}
}

void Renderer::render(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& view,
                      unsigned width, unsigned height) {
	(void)width;
	(void)height;

	wgpu::RenderPassColorAttachment colorAttachment{};
	colorAttachment.view = view;
	colorAttachment.resolveTarget = nullptr;
	colorAttachment.loadOp = wgpu::LoadOp::Clear;
	colorAttachment.storeOp = wgpu::StoreOp::Store;
	colorAttachment.clearValue = wgpu::Color{0.02, 0.03, 0.07, 1.0};
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

	wgpu::RenderPassDescriptor passDescriptor{};
	passDescriptor.label = wgpu::StringView("offscreen pass");
	passDescriptor.colorAttachmentCount = 1;
	passDescriptor.colorAttachments = &colorAttachment;
	passDescriptor.depthStencilAttachment = nullptr;
	passDescriptor.occlusionQuerySet = nullptr;
	passDescriptor.timestampWrites = nullptr;

	const auto pass = encoder.beginRenderPass(passDescriptor);
	pass.setPipeline(pipeline_);
	pass.draw(3, 1, 0, 0);
	pass.end();
}

void Renderer::release() {
	if (pipeline_) pipeline_.release();
	if (shaderModule_) shaderModule_.release();
	pipeline_ = wgpu::RenderPipeline();
	shaderModule_ = wgpu::ShaderModule();
}

} // namespace secgui
