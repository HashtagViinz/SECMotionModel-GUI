#include "render/Renderer.hpp"

#include "util/Log.hpp"

#include <cstring>
#include <stdexcept>

namespace secgui {

namespace {

constexpr const char* kSceneShader = R"(
struct Uniforms {
	viewProj: mat4x4f,
	right: vec4f,
	up: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct LineOut {
	@builtin(position) pos: vec4f,
	@location(0) color: vec4f,
};

@vertex
fn vs_line(@location(0) position: vec3f, @location(1) color: vec4f) -> LineOut {
	var out: LineOut;
	out.pos = uniforms.viewProj * vec4f(position, 1.0);
	out.color = color;
	return out;
}

@fragment
fn fs_line(@location(0) color: vec4f) -> @location(0) vec4f {
	return color;
}

struct PointOut {
	@builtin(position) pos: vec4f,
	@location(0) color: vec4f,
	@location(1) uv: vec2f,
};

@vertex
fn vs_point(
	@builtin(vertex_index) vertexIndex: u32,
	@location(0) center: vec3f,
	@location(1) color: vec4f,
	@location(2) size: f32,
) -> PointOut {
	var corners = array<vec2f, 6>(
		vec2f(-0.5, -0.5), vec2f(0.5, -0.5), vec2f(-0.5, 0.5),
		vec2f(-0.5, 0.5), vec2f(0.5, -0.5), vec2f(0.5, 0.5)
	);
	let corner = corners[vertexIndex];
	let world = center + uniforms.right.xyz * (corner.x * size) + uniforms.up.xyz * (corner.y * size);
	var out: PointOut;
	out.pos = uniforms.viewProj * vec4f(world, 1.0);
	out.color = color;
	out.uv = corner;
	return out;
}

@fragment
fn fs_point(@location(0) color: vec4f, @location(1) uv: vec2f) -> @location(0) vec4f {
	let distanceFromCenter = length(uv);
	if (distanceFromCenter > 0.5) {
		discard;
	}
	let edge = smoothstep(0.5, 0.42, distanceFromCenter);
	return vec4f(color.rgb, color.a * edge);
}
)";

struct Uniforms {
	float viewProjection[16];
	float right[4];
	float up[4];
};
static_assert(sizeof(Uniforms) == 96, "uniform layout must match WGSL std140");

std::size_t alignUp(std::size_t value, std::size_t alignment) {
	return ((value + alignment - 1) / alignment) * alignment;
}

} // namespace

wgpu::Buffer Renderer::createVertexBuffer(const char* label, std::size_t size) const {
	wgpu::BufferDescriptor descriptor{};
	descriptor.label = wgpu::StringView(label);
	descriptor.size = size;
	descriptor.usage = static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
	descriptor.mappedAtCreation = false;
	return device_.createBuffer(descriptor);
}

void Renderer::init(const wgpu::Device& device, const wgpu::Queue& queue, wgpu::TextureFormat format) {
	device_ = device;
	queue_ = queue;
	colorFormat_ = format;

	wgpu::ShaderSourceWGSL wgsl{};
	wgsl.setDefault();
	wgsl.code = wgpu::StringView(kSceneShader);
	wgpu::ShaderModuleDescriptor moduleDescriptor{};
	moduleDescriptor.nextInChain = &wgsl.chain;
	moduleDescriptor.label = wgpu::StringView("scene shader");
	wgpu::ShaderModule shaderModule = device_.createShaderModule(moduleDescriptor);
	if (!shaderModule) {
		throw std::runtime_error("failed to create the scene shader module");
	}
	lineModule_ = shaderModule;
	pointModule_ = shaderModule;

	wgpu::BufferDescriptor uniformDescriptor{};
	uniformDescriptor.label = wgpu::StringView("scene uniforms");
	uniformDescriptor.size = sizeof(Uniforms);
	uniformDescriptor.usage = static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
	uniformDescriptor.mappedAtCreation = false;
	uniformBuffer_ = device_.createBuffer(uniformDescriptor);

	wgpu::BindGroupLayoutEntry binding{};
	binding.binding = 0;
	binding.visibility = static_cast<WGPUShaderStage>(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
	binding.buffer.type = wgpu::BufferBindingType::Uniform;
	binding.buffer.hasDynamicOffset = false;
	binding.buffer.minBindingSize = sizeof(Uniforms);

	wgpu::BindGroupLayoutDescriptor layoutDescriptor{};
	layoutDescriptor.label = wgpu::StringView("scene bind group layout");
	layoutDescriptor.entryCount = 1;
	layoutDescriptor.entries = &binding;
	uniformLayout_ = device_.createBindGroupLayout(layoutDescriptor);

	wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{};
	pipelineLayoutDescriptor.label = wgpu::StringView("scene pipeline layout");
	WGPUBindGroupLayout rawLayout = uniformLayout_;
	pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
	pipelineLayoutDescriptor.bindGroupLayouts = &rawLayout;
	pipelineLayout_ = device_.createPipelineLayout(pipelineLayoutDescriptor);

	wgpu::BindGroupEntry entry{};
	entry.binding = 0;
	entry.buffer = uniformBuffer_;
	entry.offset = 0;
	entry.size = sizeof(Uniforms);
	wgpu::BindGroupDescriptor bindGroupDescriptor{};
	bindGroupDescriptor.label = wgpu::StringView("scene bind group");
	bindGroupDescriptor.layout = uniformLayout_;
	bindGroupDescriptor.entryCount = 1;
	bindGroupDescriptor.entries = &entry;
	uniformBindGroup_ = device_.createBindGroup(bindGroupDescriptor);

	wgpu::BlendState blend{};
	blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
	blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blend.color.operation = wgpu::BlendOperation::Add;
	blend.alpha.srcFactor = wgpu::BlendFactor::One;
	blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
	blend.alpha.operation = wgpu::BlendOperation::Add;

	wgpu::ColorTargetState target{};
	target.format = format;
	target.blend = &blend;
	target.writeMask = WGPUColorWriteMask_All;

	wgpu::DepthStencilState depth{};
	depth.format = wgpu::TextureFormat::Depth24Plus;
	depth.depthWriteEnabled = wgpu::OptionalBool::True;
	depth.depthCompare = wgpu::CompareFunction::LessEqual;
	depth.stencilBack.compare = wgpu::CompareFunction::Always;
	depth.stencilFront.compare = wgpu::CompareFunction::Always;
	depth.stencilReadMask = 0xFFu;
	depth.stencilWriteMask = 0xFFu;

	// Line pipeline.
	wgpu::VertexAttribute lineAttributes[2]{};
	lineAttributes[0].format = wgpu::VertexFormat::Float32x3;
	lineAttributes[0].offset = 0;
	lineAttributes[0].shaderLocation = 0;
	lineAttributes[1].format = wgpu::VertexFormat::Float32x4;
	lineAttributes[1].offset = 12;
	lineAttributes[1].shaderLocation = 1;

	wgpu::VertexBufferLayout lineBufferLayout{};
	lineBufferLayout.arrayStride = sizeof(LineVertex);
	lineBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
	lineBufferLayout.attributeCount = 2;
	lineBufferLayout.attributes = lineAttributes;

	wgpu::VertexState lineVertex{};
	lineVertex.module = lineModule_;
	lineVertex.entryPoint = wgpu::StringView("vs_line");
	lineVertex.bufferCount = 1;
	lineVertex.buffers = &lineBufferLayout;

	wgpu::FragmentState lineFragment{};
	lineFragment.module = lineModule_;
	lineFragment.entryPoint = wgpu::StringView("fs_line");
	lineFragment.targetCount = 1;
	lineFragment.targets = &target;

	wgpu::RenderPipelineDescriptor lineDescriptor{};
	lineDescriptor.label = wgpu::StringView("line pipeline");
	lineDescriptor.layout = pipelineLayout_;
	lineDescriptor.vertex = lineVertex;
	lineDescriptor.primitive.topology = wgpu::PrimitiveTopology::LineList;
	lineDescriptor.primitive.cullMode = wgpu::CullMode::None;
	lineDescriptor.fragment = &lineFragment;
	lineDescriptor.depthStencil = &depth;
	lineDescriptor.multisample.count = 1;
	lineDescriptor.multisample.mask = 0xFFFFFFFFu;
	linePipeline_ = device_.createRenderPipeline(lineDescriptor);

	// Point pipeline.
	wgpu::VertexAttribute pointAttributes[3]{};
	pointAttributes[0].format = wgpu::VertexFormat::Float32x3;
	pointAttributes[0].offset = 0;
	pointAttributes[0].shaderLocation = 0;
	pointAttributes[1].format = wgpu::VertexFormat::Float32x4;
	pointAttributes[1].offset = 12;
	pointAttributes[1].shaderLocation = 1;
	pointAttributes[2].format = wgpu::VertexFormat::Float32;
	pointAttributes[2].offset = 28;
	pointAttributes[2].shaderLocation = 2;

	wgpu::VertexBufferLayout pointBufferLayout{};
	pointBufferLayout.arrayStride = sizeof(PointInstance);
	pointBufferLayout.stepMode = wgpu::VertexStepMode::Instance;
	pointBufferLayout.attributeCount = 3;
	pointBufferLayout.attributes = pointAttributes;

	wgpu::VertexState pointVertex{};
	pointVertex.module = pointModule_;
	pointVertex.entryPoint = wgpu::StringView("vs_point");
	pointVertex.bufferCount = 1;
	pointVertex.buffers = &pointBufferLayout;

	wgpu::FragmentState pointFragment{};
	pointFragment.module = pointModule_;
	pointFragment.entryPoint = wgpu::StringView("fs_point");
	pointFragment.targetCount = 1;
	pointFragment.targets = &target;

	wgpu::RenderPipelineDescriptor pointDescriptor{};
	pointDescriptor.label = wgpu::StringView("point pipeline");
	pointDescriptor.layout = pipelineLayout_;
	pointDescriptor.vertex = pointVertex;
	pointDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	pointDescriptor.primitive.cullMode = wgpu::CullMode::None;
	pointDescriptor.fragment = &pointFragment;
	pointDescriptor.depthStencil = &depth;
	pointDescriptor.multisample.count = 1;
	pointDescriptor.multisample.mask = 0xFFFFFFFFu;
	pointPipeline_ = device_.createRenderPipeline(pointDescriptor);

	if (!linePipeline_ || !pointPipeline_) {
		throw std::runtime_error("failed to create the scene render pipelines");
	}
}

void Renderer::setEarthGrid(std::vector<LineVertex> vertices) {
	earthGrid_ = std::move(vertices);
	earthGridCount_ = earthGrid_.size();
	if (earthGridCount_ == 0) {
		return;
	}
	if (earthGridBuffer_) {
		earthGridBuffer_.release();
	}
	earthGridBuffer_ = createVertexBuffer("earth grid", earthGrid_.size() * sizeof(LineVertex));
	queue_.writeBuffer(earthGridBuffer_, 0, earthGrid_.data(), earthGrid_.size() * sizeof(LineVertex));
}

void Renderer::update(const RenderFrame& frame) {
	Uniforms uniforms{};
	std::memcpy(uniforms.viewProjection, frame.viewProjection.m.data(), sizeof(uniforms.viewProjection));
	uniforms.right[0] = static_cast<float>(frame.cameraRight.x);
	uniforms.right[1] = static_cast<float>(frame.cameraRight.y);
	uniforms.right[2] = static_cast<float>(frame.cameraRight.z);
	uniforms.right[3] = 0.0f;
	uniforms.up[0] = static_cast<float>(frame.cameraUp.x);
	uniforms.up[1] = static_cast<float>(frame.cameraUp.y);
	uniforms.up[2] = static_cast<float>(frame.cameraUp.z);
	uniforms.up[3] = 0.0f;
	queue_.writeBuffer(uniformBuffer_, 0, &uniforms, sizeof(uniforms));

	dynamicLineCount_ = frame.lines.size();
	if (dynamicLineCount_ > dynamicLineCapacity_) {
		dynamicLineCapacity_ = alignUp(dynamicLineCount_, 4096);
		if (dynamicLineBuffer_) {
			dynamicLineBuffer_.release();
		}
		dynamicLineBuffer_ = createVertexBuffer("dynamic lines", dynamicLineCapacity_ * sizeof(LineVertex));
	}
	if (dynamicLineCount_ > 0) {
		queue_.writeBuffer(dynamicLineBuffer_, 0, frame.lines.data(), dynamicLineCount_ * sizeof(LineVertex));
	}

	pointCount_ = frame.points.size();
	if (pointCount_ > pointCapacity_) {
		pointCapacity_ = alignUp(pointCount_, 4096);
		if (pointBuffer_) {
			pointBuffer_.release();
		}
		pointBuffer_ = createVertexBuffer("point instances", pointCapacity_ * sizeof(PointInstance));
	}
	if (pointCount_ > 0) {
		queue_.writeBuffer(pointBuffer_, 0, frame.points.data(), pointCount_ * sizeof(PointInstance));
	}
}

void Renderer::render(const wgpu::CommandEncoder& encoder,
                      const wgpu::TextureView& colorView,
                      const wgpu::TextureView& depthView,
                      unsigned width, unsigned height) {
	(void)width;
	(void)height;

	wgpu::RenderPassColorAttachment colorAttachment{};
	colorAttachment.view = colorView;
	colorAttachment.resolveTarget = nullptr;
	colorAttachment.loadOp = wgpu::LoadOp::Clear;
	colorAttachment.storeOp = wgpu::StoreOp::Store;
	colorAttachment.clearValue = wgpu::Color{0.015, 0.02, 0.05, 1.0};
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

	wgpu::RenderPassDepthStencilAttachment depthAttachment{};
	depthAttachment.view = depthView;
	depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
	depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
	depthAttachment.depthClearValue = 1.0f;
	depthAttachment.depthReadOnly = false;
	depthAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
	depthAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;

	wgpu::RenderPassDescriptor passDescriptor{};
	passDescriptor.label = wgpu::StringView("scene pass");
	passDescriptor.colorAttachmentCount = 1;
	passDescriptor.colorAttachments = &colorAttachment;
	passDescriptor.depthStencilAttachment = &depthAttachment;
	passDescriptor.occlusionQuerySet = nullptr;
	passDescriptor.timestampWrites = nullptr;

	const auto pass = encoder.beginRenderPass(passDescriptor);
	pass.setBindGroup(0, uniformBindGroup_, 0, nullptr);

	if (earthGridCount_ > 0 && earthGridBuffer_) {
		pass.setPipeline(linePipeline_);
		pass.setVertexBuffer(0, earthGridBuffer_, 0, earthGrid_.size() * sizeof(LineVertex));
		pass.draw(static_cast<uint32_t>(earthGridCount_), 1, 0, 0);
	}
	if (dynamicLineCount_ > 0 && dynamicLineBuffer_) {
		pass.setPipeline(linePipeline_);
		pass.setVertexBuffer(0, dynamicLineBuffer_, 0, dynamicLineCount_ * sizeof(LineVertex));
		pass.draw(static_cast<uint32_t>(dynamicLineCount_), 1, 0, 0);
	}
	if (pointCount_ > 0 && pointBuffer_) {
		pass.setPipeline(pointPipeline_);
		pass.setVertexBuffer(0, pointBuffer_, 0, pointCount_ * sizeof(PointInstance));
		pass.draw(6, static_cast<uint32_t>(pointCount_), 0, 0);
	}
	pass.end();
}

void Renderer::release() {
	if (earthGridBuffer_) earthGridBuffer_.release();
	if (dynamicLineBuffer_) dynamicLineBuffer_.release();
	if (pointBuffer_) pointBuffer_.release();
	if (uniformBindGroup_) uniformBindGroup_.release();
	if (pipelineLayout_) pipelineLayout_.release();
	if (uniformLayout_) uniformLayout_.release();
	if (uniformBuffer_) uniformBuffer_.release();
	if (linePipeline_) linePipeline_.release();
	if (pointPipeline_) pointPipeline_.release();
	if (lineModule_) lineModule_.release();
	earthGridBuffer_ = wgpu::Buffer();
	dynamicLineBuffer_ = wgpu::Buffer();
	pointBuffer_ = wgpu::Buffer();
	uniformBindGroup_ = wgpu::BindGroup();
	pipelineLayout_ = wgpu::PipelineLayout();
	uniformLayout_ = wgpu::BindGroupLayout();
	uniformBuffer_ = wgpu::Buffer();
	linePipeline_ = wgpu::RenderPipeline();
	pointPipeline_ = wgpu::RenderPipeline();
	lineModule_ = wgpu::ShaderModule();
	pointModule_ = wgpu::ShaderModule();
	earthGrid_.clear();
	earthGridCount_ = 0;
	dynamicLineCount_ = 0;
	dynamicLineCapacity_ = 0;
	pointCount_ = 0;
	pointCapacity_ = 0;
}

} // namespace secgui
