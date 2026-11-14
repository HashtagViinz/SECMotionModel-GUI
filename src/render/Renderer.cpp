#include "render/Renderer.hpp"

#include "render/EarthTextureData.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <stb_image.h>

namespace secgui {

namespace {

constexpr const char* kSceneShader = R"(
struct Uniforms {
	viewProj: mat4x4f,
	right: vec4f,
	up: vec4f,
	eye: vec4f,
	sun: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var earthTexture: texture_2d<f32>;
@group(1) @binding(1) var earthSampler: sampler;

struct LineOut {
	@builtin(position) pos: vec4f,
	@location(0) color: vec4f,
};

@vertex
fn vs_line(@location(0) position: vec3f, @location(1) color: vec4f) -> LineOut {
	var out: LineOut;
	out.pos = uniforms.viewProj * vec4f(position, 1.0);
	// Bias surface overlays (grid, route, dome) towards the camera so they
	// win against the opaque globe instead of z-fighting.
	out.pos.z -= 0.00002 * out.pos.w;
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

struct GlobeOut {
	@builtin(position) pos: vec4f,
	@location(0) normal: vec3f,
	@location(1) uv: vec2f,
	@location(2) world: vec3f,
};

@vertex
fn vs_globe(
	@location(0) position: vec3f,
	@location(1) normal: vec3f,
	@location(2) uv: vec2f,
) -> GlobeOut {
	let world = position * uniforms.eye.w;
	var out: GlobeOut;
	out.pos = uniforms.viewProj * vec4f(world, 1.0);
	out.normal = normal;
	out.uv = uv;
	out.world = world;
	return out;
}

@fragment
fn fs_globe(in: GlobeOut) -> @location(0) vec4f {
	let albedo = textureSample(earthTexture, earthSampler, in.uv).rgb;
	let normal = normalize(in.normal);
	let sun = normalize(uniforms.sun.xyz);
	let diffuse = max(dot(normal, sun), 0.0);
	// Ambient keeps the night side readable while orbiting.
	let daylight = 0.38 + 0.78 * diffuse;
	var color = albedo * daylight;
	let viewDir = normalize(uniforms.eye.xyz - in.world);
	let rim = pow(1.0 - clamp(dot(normal, viewDir), 0.0, 1.0), 4.0);
	color += vec3f(0.10, 0.24, 0.55) * rim * 0.55;
	return vec4f(color, 1.0);
}

struct AtmosphereOut {
	@builtin(position) pos: vec4f,
	@location(0) normal: vec3f,
	@location(1) world: vec3f,
};

@vertex
fn vs_atmosphere(
	@location(0) position: vec3f,
	@location(1) normal: vec3f,
	@location(2) uv: vec2f,
) -> AtmosphereOut {
	// A slightly larger shell around the globe produces the halo.
	let world = position * (uniforms.eye.w * 1.028);
	var out: AtmosphereOut;
	out.pos = uniforms.viewProj * vec4f(world, 1.0);
	out.normal = normal;
	out.world = world;
	return out;
}

@fragment
fn fs_atmosphere(in: AtmosphereOut) -> @location(0) vec4f {
	let viewDir = normalize(uniforms.eye.xyz - in.world);
	let facing = dot(normalize(in.normal), viewDir);
	// Keep only the far side of the shell: the globe already hides it inside
	// the disk, so what remains is the thin ring outside the horizon.
	if (facing >= 0.0) {
		discard;
	}
	let rim = pow(1.0 - clamp(-facing, 0.0, 1.0), 3.0);
	let sun = normalize(uniforms.sun.xyz);
	let lit = clamp(dot(normalize(in.normal), sun) * 0.5 + 0.5, 0.0, 1.0);
	let intensity = rim * (0.35 + 0.65 * lit);
	return vec4f(vec3f(0.22, 0.52, 1.0) * intensity * 1.35, 1.0);
}
)";

struct Uniforms {
	float viewProjection[16];
	float right[4];
	float up[4];
	float eye[4];
	float sun[4];
};
static_assert(sizeof(Uniforms) == 128, "uniform layout must match WGSL std140");

std::size_t alignUp(std::size_t value, std::size_t alignment) {
	return ((value + alignment - 1) / alignment) * alignment;
}

struct GlobeMesh {
	std::vector<GlobeVertex> vertices;
	std::vector<uint32_t> indices;
};

GlobeMesh makeGlobeMesh(unsigned stacks, unsigned slices) {
	GlobeMesh mesh;
	mesh.vertices.reserve(static_cast<std::size_t>(stacks + 1) * (slices + 1));
	mesh.indices.reserve(static_cast<std::size_t>(stacks) * slices * 6);
	constexpr double kPi = 3.14159265358979323846;
	for (unsigned y = 0; y <= stacks; ++y) {
		const double v = static_cast<double>(y) / static_cast<double>(stacks);
		const double phi = kPi * 0.5 - v * kPi;
		const double cosPhi = std::cos(phi);
		const double sinPhi = std::sin(phi);
		for (unsigned x = 0; x <= slices; ++x) {
			const double u = static_cast<double>(x) / static_cast<double>(slices);
			const double lambda = (u - 0.5) * 2.0 * kPi;
			const double cosLambda = std::cos(lambda);
			const double sinLambda = std::sin(lambda);
			GlobeVertex vertex;
			vertex.x = static_cast<float>(cosPhi * cosLambda);
			vertex.y = static_cast<float>(cosPhi * sinLambda);
			vertex.z = static_cast<float>(sinPhi);
			vertex.nx = vertex.x;
			vertex.ny = vertex.y;
			vertex.nz = vertex.z;
			vertex.u = static_cast<float>(u);
			vertex.v = static_cast<float>(v);
			mesh.vertices.push_back(vertex);
		}
	}
	for (unsigned y = 0; y < stacks; ++y) {
		for (unsigned x = 0; x < slices; ++x) {
			const uint32_t a = y * (slices + 1) + x;
			const uint32_t b = a + 1;
			const uint32_t c = a + slices + 1;
			const uint32_t d = c + 1;
			mesh.indices.push_back(a);
			mesh.indices.push_back(c);
			mesh.indices.push_back(b);
			mesh.indices.push_back(b);
			mesh.indices.push_back(c);
			mesh.indices.push_back(d);
		}
	}
	return mesh;
}

} // namespace

wgpu::Buffer Renderer::createBuffer(const char* label, std::size_t size,
                                    WGPUBufferUsage usage) const {
	wgpu::BufferDescriptor descriptor{};
	descriptor.label = wgpu::StringView(label);
	descriptor.size = size;
	descriptor.usage = usage;
	descriptor.mappedAtCreation = false;
	return device_.createBuffer(descriptor);
}

wgpu::Buffer Renderer::createVertexBuffer(const char* label, std::size_t size) const {
	return createBuffer(label, size,
	                    static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst));
}

wgpu::Texture Renderer::createEarthTexture() const {
	int width = 0;
	int height = 0;
	int channels = 0;
	unsigned char* pixels = stbi_load_from_memory(
		kEarthTextureJpeg, static_cast<int>(kEarthTextureJpegSize),
		&width, &height, &channels, 4);

	std::vector<unsigned char> fallback;
	if (pixels == nullptr || width <= 0 || height <= 0) {
		logWarn("failed to decode the embedded Earth texture; using a flat fallback");
		width = 1;
		height = 1;
		fallback = {30, 62, 120, 255};
		pixels = fallback.data();
	} else {
		logInfo("Earth texture decoded: " + std::to_string(width) + "x"
		        + std::to_string(height) + " (" + std::to_string(channels) + " channels)");
	}

	wgpu::TextureDescriptor descriptor{};
	descriptor.label = wgpu::StringView("Earth texture");
	descriptor.size = wgpu::Extent3D{static_cast<uint32_t>(width),
	                                 static_cast<uint32_t>(height), 1};
	descriptor.mipLevelCount = 1;
	descriptor.sampleCount = 1;
	descriptor.dimension = wgpu::TextureDimension::_2D;
	descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
	descriptor.usage = static_cast<WGPUTextureUsage>(
		WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
	const wgpu::Texture texture = device_.createTexture(descriptor);
	if (!texture) {
		if (pixels != fallback.data()) {
			stbi_image_free(pixels);
		}
		throw std::runtime_error("failed to create the Earth texture");
	}

	wgpu::TexelCopyTextureInfo destination{};
	destination.texture = texture;
	destination.mipLevel = 0;
	destination.origin = wgpu::Origin3D{0, 0, 0};
	destination.aspect = wgpu::TextureAspect::All;

	wgpu::TexelCopyBufferLayout layout{};
	layout.offset = 0;
	layout.bytesPerRow = static_cast<uint32_t>(width) * 4u;
	layout.rowsPerImage = static_cast<uint32_t>(height);

	wgpu::Extent3D writeSize{static_cast<uint32_t>(width),
	                         static_cast<uint32_t>(height), 1};
	queue_.writeTexture(destination, pixels,
	                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
	                    layout, writeSize);

	if (pixels != fallback.data()) {
		stbi_image_free(pixels);
	}
	return texture;
}

wgpu::Sampler Renderer::createEarthSampler() const {
	wgpu::SamplerDescriptor descriptor{};
	descriptor.label = wgpu::StringView("Earth sampler");
	descriptor.addressModeU = wgpu::AddressMode::Repeat;
	descriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
	descriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
	descriptor.magFilter = wgpu::FilterMode::Linear;
	descriptor.minFilter = wgpu::FilterMode::Linear;
	descriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
	descriptor.lodMinClamp = 0.0f;
	descriptor.lodMaxClamp = 1.0f;
	descriptor.compare = wgpu::CompareFunction::Undefined;
	descriptor.maxAnisotropy = 1;
	return device_.createSampler(descriptor);
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
	sceneModule_ = device_.createShaderModule(moduleDescriptor);
	if (!sceneModule_) {
		throw std::runtime_error("failed to create the scene shader module");
	}

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

	earthTexture_ = createEarthTexture();
	earthTextureView_ = earthTexture_.createView();
	earthSampler_ = createEarthSampler();

	wgpu::BindGroupLayoutEntry textureBindings[2]{};
	textureBindings[0].binding = 0;
	textureBindings[0].visibility = wgpu::ShaderStage::Fragment;
	textureBindings[0].texture.sampleType = wgpu::TextureSampleType::Float;
	textureBindings[0].texture.viewDimension = wgpu::TextureViewDimension::_2D;
	textureBindings[0].texture.multisampled = false;
	textureBindings[1].binding = 1;
	textureBindings[1].visibility = wgpu::ShaderStage::Fragment;
	textureBindings[1].sampler.type = wgpu::SamplerBindingType::Filtering;

	wgpu::BindGroupLayoutDescriptor textureLayoutDescriptor{};
	textureLayoutDescriptor.label = wgpu::StringView("Earth bind group layout");
	textureLayoutDescriptor.entryCount = 2;
	textureLayoutDescriptor.entries = textureBindings;
	textureLayout_ = device_.createBindGroupLayout(textureLayoutDescriptor);

	wgpu::BindGroupEntry earthEntries[2]{};
	earthEntries[0].binding = 0;
	earthEntries[0].textureView = earthTextureView_;
	earthEntries[1].binding = 1;
	earthEntries[1].sampler = earthSampler_;

	wgpu::BindGroupDescriptor earthBindGroupDescriptor{};
	earthBindGroupDescriptor.label = wgpu::StringView("Earth bind group");
	earthBindGroupDescriptor.layout = textureLayout_;
	earthBindGroupDescriptor.entryCount = 2;
	earthBindGroupDescriptor.entries = earthEntries;
	earthBindGroup_ = device_.createBindGroup(earthBindGroupDescriptor);

	WGPUBindGroupLayout pipelineLayouts[2] = {uniformLayout_, textureLayout_};
	wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{};
	pipelineLayoutDescriptor.label = wgpu::StringView("scene pipeline layout");
	pipelineLayoutDescriptor.bindGroupLayoutCount = 2;
	pipelineLayoutDescriptor.bindGroupLayouts = pipelineLayouts;
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

	wgpu::ColorTargetState opaqueTarget{};
	opaqueTarget.format = format;
	opaqueTarget.blend = nullptr;
	opaqueTarget.writeMask = WGPUColorWriteMask_All;

	wgpu::BlendState additiveBlend{};
	additiveBlend.color.srcFactor = wgpu::BlendFactor::One;
	additiveBlend.color.dstFactor = wgpu::BlendFactor::One;
	additiveBlend.color.operation = wgpu::BlendOperation::Add;
	additiveBlend.alpha.srcFactor = wgpu::BlendFactor::One;
	additiveBlend.alpha.dstFactor = wgpu::BlendFactor::One;
	additiveBlend.alpha.operation = wgpu::BlendOperation::Add;

	wgpu::ColorTargetState atmosphereTarget{};
	atmosphereTarget.format = format;
	atmosphereTarget.blend = &additiveBlend;
	atmosphereTarget.writeMask = WGPUColorWriteMask_All;

	wgpu::DepthStencilState depth{};
	depth.format = wgpu::TextureFormat::Depth24Plus;
	depth.depthWriteEnabled = wgpu::OptionalBool::True;
	depth.depthCompare = wgpu::CompareFunction::LessEqual;
	depth.stencilBack.compare = wgpu::CompareFunction::Always;
	depth.stencilFront.compare = wgpu::CompareFunction::Always;
	depth.stencilReadMask = 0xFFu;
	depth.stencilWriteMask = 0xFFu;

	wgpu::DepthStencilState atmosphereDepth = depth;
	atmosphereDepth.depthWriteEnabled = wgpu::OptionalBool::False;

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
	lineVertex.module = sceneModule_;
	lineVertex.entryPoint = wgpu::StringView("vs_line");
	lineVertex.bufferCount = 1;
	lineVertex.buffers = &lineBufferLayout;

	wgpu::FragmentState lineFragment{};
	lineFragment.module = sceneModule_;
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
	pointVertex.module = sceneModule_;
	pointVertex.entryPoint = wgpu::StringView("vs_point");
	pointVertex.bufferCount = 1;
	pointVertex.buffers = &pointBufferLayout;

	wgpu::FragmentState pointFragment{};
	pointFragment.module = sceneModule_;
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

	// Globe and atmosphere share the sphere mesh and the vertex layout.
	wgpu::VertexAttribute globeAttributes[3]{};
	globeAttributes[0].format = wgpu::VertexFormat::Float32x3;
	globeAttributes[0].offset = 0;
	globeAttributes[0].shaderLocation = 0;
	globeAttributes[1].format = wgpu::VertexFormat::Float32x3;
	globeAttributes[1].offset = 12;
	globeAttributes[1].shaderLocation = 1;
	globeAttributes[2].format = wgpu::VertexFormat::Float32x2;
	globeAttributes[2].offset = 24;
	globeAttributes[2].shaderLocation = 2;

	wgpu::VertexBufferLayout globeLayout{};
	globeLayout.arrayStride = sizeof(GlobeVertex);
	globeLayout.stepMode = wgpu::VertexStepMode::Vertex;
	globeLayout.attributeCount = 3;
	globeLayout.attributes = globeAttributes;

	wgpu::VertexState globeVertex{};
	globeVertex.module = sceneModule_;
	globeVertex.entryPoint = wgpu::StringView("vs_globe");
	globeVertex.bufferCount = 1;
	globeVertex.buffers = &globeLayout;

	wgpu::FragmentState globeFragment{};
	globeFragment.module = sceneModule_;
	globeFragment.entryPoint = wgpu::StringView("fs_globe");
	globeFragment.targetCount = 1;
	globeFragment.targets = &opaqueTarget;

	wgpu::RenderPipelineDescriptor globeDescriptor{};
	globeDescriptor.label = wgpu::StringView("globe pipeline");
	globeDescriptor.layout = pipelineLayout_;
	globeDescriptor.vertex = globeVertex;
	globeDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	globeDescriptor.primitive.cullMode = wgpu::CullMode::None;
	globeDescriptor.fragment = &globeFragment;
	globeDescriptor.depthStencil = &depth;
	globeDescriptor.multisample.count = 1;
	globeDescriptor.multisample.mask = 0xFFFFFFFFu;
	globePipeline_ = device_.createRenderPipeline(globeDescriptor);

	wgpu::VertexState atmosphereVertex = globeVertex;
	atmosphereVertex.entryPoint = wgpu::StringView("vs_atmosphere");

	wgpu::FragmentState atmosphereFragment{};
	atmosphereFragment.module = sceneModule_;
	atmosphereFragment.entryPoint = wgpu::StringView("fs_atmosphere");
	atmosphereFragment.targetCount = 1;
	atmosphereFragment.targets = &atmosphereTarget;

	wgpu::RenderPipelineDescriptor atmosphereDescriptor{};
	atmosphereDescriptor.label = wgpu::StringView("atmosphere pipeline");
	atmosphereDescriptor.layout = pipelineLayout_;
	atmosphereDescriptor.vertex = atmosphereVertex;
	atmosphereDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	atmosphereDescriptor.primitive.cullMode = wgpu::CullMode::None;
	atmosphereDescriptor.fragment = &atmosphereFragment;
	atmosphereDescriptor.depthStencil = &atmosphereDepth;
	atmosphereDescriptor.multisample.count = 1;
	atmosphereDescriptor.multisample.mask = 0xFFFFFFFFu;
	atmospherePipeline_ = device_.createRenderPipeline(atmosphereDescriptor);

	if (!linePipeline_ || !pointPipeline_ || !globePipeline_ || !atmospherePipeline_) {
		throw std::runtime_error("failed to create the scene render pipelines");
	}

	const GlobeMesh mesh = makeGlobeMesh(64, 128);
	globeVertexCount_ = mesh.vertices.size();
	globeIndexCount_ = mesh.indices.size();
	globeTriangleCount_ = globeIndexCount_ / 3;
	globeVertexBuffer_ = createVertexBuffer(
		"globe vertices", globeVertexCount_ * sizeof(GlobeVertex));
	queue_.writeBuffer(globeVertexBuffer_, 0, mesh.vertices.data(),
	                   globeVertexCount_ * sizeof(GlobeVertex));
	globeIndexBuffer_ = createBuffer(
		"globe indices", globeIndexCount_ * sizeof(uint32_t),
		static_cast<WGPUBufferUsage>(WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst));
	queue_.writeBuffer(globeIndexBuffer_, 0, mesh.indices.data(),
	                   globeIndexCount_ * sizeof(uint32_t));
}

void Renderer::setEarthGrid(std::vector<LineVertex> vertices) {
	earthGridCount_ = vertices.size();
	if (earthGridBuffer_) {
		earthGridBuffer_.release();
		earthGridBuffer_ = wgpu::Buffer();
	}
	if (earthGridCount_ == 0) {
		return;
	}
	earthGrid_ = std::move(vertices);
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
	uniforms.eye[0] = static_cast<float>(frame.cameraEye.x);
	uniforms.eye[1] = static_cast<float>(frame.cameraEye.y);
	uniforms.eye[2] = static_cast<float>(frame.cameraEye.z);
	uniforms.eye[3] = static_cast<float>(frame.earthRadiusKm);
	uniforms.sun[0] = static_cast<float>(frame.sunDirection.x);
	uniforms.sun[1] = static_cast<float>(frame.sunDirection.y);
	uniforms.sun[2] = static_cast<float>(frame.sunDirection.z);
	uniforms.sun[3] = 0.0f;
	queue_.writeBuffer(uniformBuffer_, 0, &uniforms, sizeof(uniforms));

	globeVisible_ = frame.globeVisible;
	atmosphereVisible_ = frame.globeVisible && frame.atmosphereVisible;
	earthRadiusKm_ = frame.earthRadiusKm;

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
	pass.setBindGroup(1, earthBindGroup_, 0, nullptr);

	if (globeVisible_ && globePipeline_ && globeIndexCount_ > 0 && earthRadiusKm_ > 0.0) {
		pass.setPipeline(globePipeline_);
		pass.setVertexBuffer(0, globeVertexBuffer_, 0,
		                     globeVertexCount_ * sizeof(GlobeVertex));
		pass.setIndexBuffer(globeIndexBuffer_, wgpu::IndexFormat::Uint32, 0,
		                    globeIndexCount_ * sizeof(uint32_t));
		pass.drawIndexed(static_cast<uint32_t>(globeIndexCount_), 1, 0, 0, 0);
	}
	if (atmosphereVisible_ && atmospherePipeline_ && globeIndexCount_ > 0 && earthRadiusKm_ > 0.0) {
		pass.setPipeline(atmospherePipeline_);
		pass.setVertexBuffer(0, globeVertexBuffer_, 0,
		                     globeVertexCount_ * sizeof(GlobeVertex));
		pass.setIndexBuffer(globeIndexBuffer_, wgpu::IndexFormat::Uint32, 0,
		                    globeIndexCount_ * sizeof(uint32_t));
		pass.drawIndexed(static_cast<uint32_t>(globeIndexCount_), 1, 0, 0, 0);
	}
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
	if (globeVertexBuffer_) globeVertexBuffer_.release();
	if (globeIndexBuffer_) globeIndexBuffer_.release();
	if (earthBindGroup_) earthBindGroup_.release();
	if (earthTextureView_) earthTextureView_.release();
	if (earthSampler_) earthSampler_.release();
	if (earthTexture_) earthTexture_.release();
	if (uniformBindGroup_) uniformBindGroup_.release();
	if (pipelineLayout_) pipelineLayout_.release();
	if (textureLayout_) textureLayout_.release();
	if (uniformLayout_) uniformLayout_.release();
	if (uniformBuffer_) uniformBuffer_.release();
	if (linePipeline_) linePipeline_.release();
	if (pointPipeline_) pointPipeline_.release();
	if (globePipeline_) globePipeline_.release();
	if (atmospherePipeline_) atmospherePipeline_.release();
	if (sceneModule_) sceneModule_.release();
	earthGridBuffer_ = wgpu::Buffer();
	dynamicLineBuffer_ = wgpu::Buffer();
	pointBuffer_ = wgpu::Buffer();
	globeVertexBuffer_ = wgpu::Buffer();
	globeIndexBuffer_ = wgpu::Buffer();
	earthBindGroup_ = wgpu::BindGroup();
	earthTextureView_ = wgpu::TextureView();
	earthSampler_ = wgpu::Sampler();
	earthTexture_ = wgpu::Texture();
	uniformBindGroup_ = wgpu::BindGroup();
	pipelineLayout_ = wgpu::PipelineLayout();
	textureLayout_ = wgpu::BindGroupLayout();
	uniformLayout_ = wgpu::BindGroupLayout();
	uniformBuffer_ = wgpu::Buffer();
	linePipeline_ = wgpu::RenderPipeline();
	pointPipeline_ = wgpu::RenderPipeline();
	globePipeline_ = wgpu::RenderPipeline();
	atmospherePipeline_ = wgpu::RenderPipeline();
	sceneModule_ = wgpu::ShaderModule();
	earthGrid_.clear();
	earthGridCount_ = 0;
	globeVertexCount_ = 0;
	globeIndexCount_ = 0;
	globeTriangleCount_ = 0;
	dynamicLineCount_ = 0;
	dynamicLineCapacity_ = 0;
	pointCount_ = 0;
	pointCapacity_ = 0;
}

} // namespace secgui
