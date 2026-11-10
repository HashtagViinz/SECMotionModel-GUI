#include "app/Application.hpp"

#include "cli/JsonlDriver.hpp"
#include "commands/CommandBus.hpp"
#include "screenshot/Screenshot.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

#ifdef SECGUI_WINDOW
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>
#include <glfw3webgpu.h>
#endif

namespace secgui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kVesselAltitudeKm = 550.0;

void configureLogLevel(const std::string& level) {
	if (level == "debug") setLogLevel(LogLevel::Debug);
	else if (level == "info") setLogLevel(LogLevel::Info);
	else if (level == "warn") setLogLevel(LogLevel::Warn);
	else if (level == "error") setLogLevel(LogLevel::Error);
}

std::filesystem::path resolveOutputPath(const std::string& outputDir, const std::string& candidate) {
	std::filesystem::path path(candidate);
	if (path.is_absolute()) {
		return path;
	}
	return std::filesystem::path(outputDir) / path;
}

Vec3 corrected(const Vec3& value, double factor) {
	return Vec3{value.x * factor, value.y * factor, value.z * factor};
}

void addLine(std::vector<LineVertex>& vertices, const Vec3& from, const Vec3& to,
             float r, float g, float b, float a) {
	vertices.push_back(LineVertex{static_cast<float>(from.x), static_cast<float>(from.y), static_cast<float>(from.z), r, g, b, a});
	vertices.push_back(LineVertex{static_cast<float>(to.x), static_cast<float>(to.y), static_cast<float>(to.z), r, g, b, a});
}

Vec3 normalized(const Vec3& value) {
	const double length = value.norm();
	if (length <= 0.0) {
		return Vec3{0.0, 0.0, 1.0};
	}
	return Vec3{value.x / length, value.y / length, value.z / length};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
	return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

void addCircleOnSphere(std::vector<LineVertex>& vertices, const Vec3& centre,
                       double angularRadiusDeg, double radiusKm, int steps,
                       float r, float g, float b, float a) {
	const Vec3 direction = normalized(centre);
	Vec3 reference{0.0, 0.0, 1.0};
	if (std::abs(direction.z) > 0.9) {
		reference = Vec3{1.0, 0.0, 0.0};
	}
	const Vec3 u = normalized(cross(direction, reference));
	const Vec3 v = cross(direction, u);
	const double alpha = angularRadiusDeg * kPi / 180.0;
	Vec3 previous;
	for (int i = 0; i <= steps; ++i) {
		const double theta = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(steps);
		const Vec3 offset{
			std::cos(theta) * u.x + std::sin(theta) * v.x,
			std::cos(theta) * u.y + std::sin(theta) * v.y,
			std::cos(theta) * u.z + std::sin(theta) * v.z,
		};
		Vec3 point{
			std::cos(alpha) * direction.x + std::sin(alpha) * offset.x,
			std::cos(alpha) * direction.y + std::sin(alpha) * offset.y,
			std::cos(alpha) * direction.z + std::sin(alpha) * offset.z,
		};
		point = corrected(point, radiusKm);
		if (i > 0) {
			addLine(vertices, previous, point, r, g, b, a);
		}
		previous = point;
	}
}

void addGreatCircleArc(std::vector<LineVertex>& vertices, const Vec3& from, const Vec3& to,
                       double radiusKm, int steps, float r, float g, float b, float a) {
	const Vec3 startVec = normalized(from);
	const Vec3 endVec = normalized(to);
	Vec3 previous;
	for (int i = 0; i <= steps; ++i) {
		const double t = static_cast<double>(i) / static_cast<double>(steps);
		Vec3 point{
			(1.0 - t) * startVec.x + t * endVec.x,
			(1.0 - t) * startVec.y + t * endVec.y,
			(1.0 - t) * startVec.z + t * endVec.z,
		};
		point = corrected(normalized(point), radiusKm);
		if (i > 0) {
			addLine(vertices, previous, point, r, g, b, a);
		}
		previous = point;
	}
}

std::vector<LineVertex> makeEarthGrid(double radiusKm) {
	std::vector<LineVertex> vertices;
	const double radius = radiusKm * 1.0005;
	const int segments = 96;
	const float r = 0.20f;
	const float g = 0.28f;
	const float b = 0.42f;
	const float a = 0.55f;
	for (int latitude = -75; latitude <= 75; latitude += 15) {
		const double phi = latitude * kPi / 180.0;
		Vec3 previous;
		for (int i = 0; i <= segments; ++i) {
			const double lambda = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(segments);
			const Vec3 point{
				radius * std::cos(phi) * std::cos(lambda),
				radius * std::cos(phi) * std::sin(lambda),
				radius * std::sin(phi),
			};
			if (i > 0) {
				addLine(vertices, previous, point, r, g, b, a);
			}
			previous = point;
		}
	}
	for (int longitude = 0; longitude < 360; longitude += 15) {
		const double lambda = longitude * kPi / 180.0;
		Vec3 previous;
		for (int i = 0; i <= segments; ++i) {
			const double phi = -kPi * 0.5 + kPi * static_cast<double>(i) / static_cast<double>(segments);
			const Vec3 point{
				radius * std::cos(phi) * std::cos(lambda),
				radius * std::cos(phi) * std::sin(lambda),
				radius * std::sin(phi),
			};
			if (i > 0) {
				addLine(vertices, previous, point, r, g, b, a);
			}
			previous = point;
		}
	}
	return vertices;
}

double elevationToAngularRadius(double elevationDeg, double earthRadiusKm, double altitudeKm) {
	const double elevation = elevationDeg * kPi / 180.0;
	const double ratio = earthRadiusKm / (earthRadiusKm + altitudeKm) * std::cos(elevation);
	const double clamped = std::clamp(ratio, -1.0, 1.0);
	return (std::acos(clamped) - elevation) * 180.0 / kPi;
}

Vec3 lerp(const Vec3& a, const Vec3& b, double t) {
	return Vec3{
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.z + (b.z - a.z) * t,
	};
}

bool validNode(const Vec3& position) {
	return position.norm() > 1.0;
}

Vec3 lookUpNode(const FrameData& frame, const std::string& name,
                const std::string& terminalName, const std::string& observerName) {
	if (name.empty()) {
		return Vec3{};
	}
	for (const SatelliteFrame& satellite : frame.satellites) {
		if (satellite.id == name) {
			return satellite.ecefKm;
		}
	}
	if (name == terminalName || name == "terminal" || name == "vessel" || name == "V-ATLANTIC") {
		return frame.vessel.ecefKm;
	}
	if (name == observerName || name == "OBS" || name == "observer" || name == "OBSERVER") {
		return frame.observer.ecefKm;
	}
	return Vec3{};
}

std::array<float, 4> taskColor(const std::string& type) {
	if (type == "CPU_Intensive") return {0.40f, 0.70f, 1.00f, 1.0f};
	if (type == "Generic_Service") return {0.50f, 1.00f, 0.60f, 1.0f};
	if (type == "CPU_and_Data_Intensive") return {0.90f, 0.60f, 1.00f, 1.0f};
	if (type == "Batch") return {1.00f, 0.80f, 0.40f, 1.0f};
	return {1.00f, 1.00f, 1.00f, 1.0f};
}

#ifdef SECGUI_WINDOW
struct ScreenPoint {
	float x = 0.0f;
	float y = 0.0f;
	bool visible = false;
};

ScreenPoint projectToScreen(const RenderFrame& frame, const Vec3& position,
                            unsigned width, unsigned height) {
	const float* m = frame.viewProjection.m.data();
	const float x = static_cast<float>(position.x);
	const float y = static_cast<float>(position.y);
	const float z = static_cast<float>(position.z);
	const float clipX = m[0] * x + m[4] * y + m[8] * z + m[12];
	const float clipY = m[1] * x + m[5] * y + m[9] * z + m[13];
	const float clipZ = m[2] * x + m[6] * y + m[10] * z + m[14];
	const float clipW = m[3] * x + m[7] * y + m[11] * z + m[15];
	if (clipW <= 1e-6f) {
		return {};
	}
	const float inverseW = 1.0f / clipW;
	const float ndcX = clipX * inverseW;
	const float ndcY = clipY * inverseW;
	const float ndcZ = clipZ * inverseW;
	if (ndcZ < -1.0f || ndcZ > 1.0f) {
		return {};
	}
	return ScreenPoint{
		(ndcX * 0.5f + 0.5f) * static_cast<float>(width),
		(1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height),
		true,
	};
}

bool earthOccludes(const Vec3& eye, const Vec3& point, double radiusKm) {
	const double dx = point.x - eye.x;
	const double dy = point.y - eye.y;
	const double dz = point.z - eye.z;
	const double a = dx * dx + dy * dy + dz * dz;
	if (a <= 1e-12) {
		return false;
	}
	const double b = 2.0 * (eye.x * dx + eye.y * dy + eye.z * dz);
	const double c = eye.x * eye.x + eye.y * eye.y + eye.z * eye.z - radiusKm * radiusKm;
	const double discriminant = b * b - 4.0 * a * c;
	if (discriminant <= 0.0) {
		return false;
	}
	const double root = std::sqrt(discriminant);
	const double first = (-b - root) / (2.0 * a);
	const double second = (-b + root) / (2.0 * a);
	return (first > 1e-4 && first < 1.0) || (second > 1e-4 && second < 1.0);
}

void drawOverlayLabel(ImDrawList* drawList, const ScreenPoint& point,
                      ImU32 color, const std::string& text) {
	if (!point.visible) {
		return;
	}
	const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
	drawList->AddRectFilled(
		ImVec2(point.x + 6.0f, point.y - 2.0f),
		ImVec2(point.x + 12.0f + textSize.x, point.y + textSize.y + 2.0f),
		IM_COL32(8, 12, 22, 190), 3.0f);
	drawList->AddText(ImVec2(point.x + 9.0f, point.y), color, text.c_str());
}
#endif

} // namespace

Application::Application(Args args)
	: args_(std::move(args)) {}

void Application::loadScene(const std::filesystem::path& path) {
	scene_ = SceneLoader::load(path);
	earthRadiusKm_ = scene_->earthRadiusKm;
	phiMaxDeg_ = scene_->phiMaxDeg;
	phiBufferDeg_ = scene_->phiBufferDeg;
	currentFrame_ = 0;
	sceneTimeS_ = 0.0;
	playing_ = false;
	quit_ = false;

	frameData_ = SceneLoader::loadFrame(*scene_, currentFrame_);
	tasks_ = SceneLoader::loadTasks(*scene_, tasksOverride_);

	if (camera_.target.empty() || camera_.target == "V-ATLANTIC") {
		camera_.target = scene_->terminalName;
	}
	buildEarthGrid();
}

void Application::buildEarthGrid() {
	if (renderer_.colorFormat() == wgpu::TextureFormat::Undefined) {
		return;
	}
	if (!layerEnabled("grid", true)) {
		renderer_.setEarthGrid({});
		return;
	}
	renderer_.setEarthGrid(makeEarthGrid(earthRadiusKm_));
}

bool Application::layerEnabled(const std::string& name, bool fallback) const {
	const auto found = layers_.find(name);
	if (found == layers_.end()) {
		return fallback;
	}
	return found->second;
}

void Application::reloadFrame() {
	if (!scene_) {
		return;
	}
	frameData_ = SceneLoader::loadFrame(*scene_, currentFrame_);
}

void Application::requireScene() const {
	if (!scene_) {
		throw CommandError("NO_SCENE", "no scene loaded; run 'open' first");
	}
}

void Application::applyInitialState() {
	if (!args_.cameraMode.empty()) {
		camera_.mode = args_.cameraMode;
	}
	if (!args_.cameraTarget.empty()) {
		camera_.target = args_.cameraTarget;
	}
	if (camera_.target.empty() && scene_) {
		camera_.target = scene_->terminalName;
	}
	if (camera_.mode == "vessel" || camera_.mode == "topdown") {
		camera_.distanceKm = 4500.0;
	} else if (camera_.mode == "satellite") {
		camera_.distanceKm = 1500.0;
	}
	for (const std::string& entry : args_.layers) {
		const auto separator = entry.find('=');
		if (separator == std::string::npos) {
			continue;
		}
		const std::string name = entry.substr(0, separator);
		const std::string value = entry.substr(separator + 1);
		layers_[name] = (value == "on" || value == "true" || value == "1");
	}
	buildEarthGrid();
	if (args_.frame >= 0) {
		setFrame(args_.frame);
	} else if (args_.time >= 0.0) {
		seekToTime(args_.time);
	}
	if (args_.playOnStart) {
		playing_ = true;
	}
}

void Application::setFrame(int frame) {
	requireScene();
	if (frame < 0 || frame >= scene_->frameCount) {
		throw CommandError("OUT_OF_RANGE",
			"frame " + std::to_string(frame) + " is outside [0, "
			+ std::to_string(scene_->frameCount - 1) + "]");
	}
	currentFrame_ = frame;
	sceneTimeS_ = scene_->frameTime(frame);
	reloadFrame();
}

void Application::seekToTime(double timeS) {
	requireScene();
	const double clamped = std::clamp(timeS, 0.0, scene_->durationS);
	const int frame = std::clamp(
		static_cast<int>(std::lround(clamped / scene_->intervalS)),
		0, scene_->frameCount - 1);
	sceneTimeS_ = clamped;
	if (frame != currentFrame_) {
		currentFrame_ = frame;
		reloadFrame();
	}
}

void Application::advancePlayback(double deltaSeconds) {
	if (!scene_ || !playing_) {
		return;
	}
	double next = sceneTimeS_ + deltaSeconds * speed_;
	if (scene_->durationS > 0.0) {
		while (next >= scene_->durationS) {
			next -= scene_->durationS;
		}
	}
	seekToTime(next);
}

void Application::recordCommand(const std::string& command, const nlohmann::json& payload) {
	if (args_.recordScriptPath.empty()) {
		return;
	}
	if (!recorder_.is_open()) {
		const std::filesystem::path path(args_.recordScriptPath);
		if (!path.parent_path().empty()) {
			std::filesystem::create_directories(path.parent_path());
		}
		recorder_.open(path, std::ios::out | std::ios::trunc);
		if (!recorder_) {
			logWarn("cannot open the record script: " + path.string());
			args_.recordScriptPath.clear();
			return;
		}
	}
	nlohmann::json line = payload;
	line["cmd"] = command;
	recorder_ << line.dump() << "\n";
	recorder_.flush();
}

nlohmann::json Application::sceneInfo() const {
	nlohmann::json info;
	if (!scene_) {
		info["loaded"] = false;
		return info;
	}
	info["loaded"] = true;
	info["manifest"] = scene_->manifestPath.string();
	info["root_dir"] = scene_->rootDir.string();
	info["version"] = scene_->version;
	info["t0"] = scene_->t0;
	info["interval_s"] = scene_->intervalS;
	info["duration_s"] = scene_->durationS;
	info["frame_count"] = scene_->frameCount;
	info["frames_pattern"] = scene_->framesPattern;
	info["earth_radius_km"] = scene_->earthRadiusKm;
	info["phi_max_deg"] = scene_->phiMaxDeg;
	info["phi_buffer_deg"] = scene_->phiBufferDeg;
	info["terminal"] = {
		{"name", scene_->terminalName},
		{"lat", scene_->terminalLat},
		{"lon", scene_->terminalLon},
	};
	info["observer"] = {
		{"name", scene_->observerName},
		{"lat", scene_->observerLat},
		{"lon", scene_->observerLon},
	};
	info["tasks"] = !tasksOverride_.empty() ? tasksOverride_ : scene_->tasksPath;
	return info;
}

void Application::printReadyLine() const {
	nlohmann::json ready;
	ready["protocol"] = 1;
	ready["event"] = "ready";
	ready["version"] = SECGUI_VERSION;
	ready["backend"] = gpu_.backendName;
	ready["adapter"] = gpu_.adapterName;
	std::cout << ready.dump() << "\n";
	std::cout.flush();
}

int Application::run() {
	configureLogLevel(args_.logLevel);

	if (args_.validateOnly) {
		try {
			if (!args_.scenePath.empty()) {
				loadScene(args_.scenePath);
			} else {
				throw std::runtime_error("--validate-only requires --scene");
			}
		} catch (const std::exception& error) {
			nlohmann::json response;
			response["protocol"] = 1;
			response["cmd"] = "validate";
			response["ok"] = false;
			response["error"] = {{"code", "SCENE_ERROR"}, {"message", error.what()}};
			std::cout << response.dump() << "\n";
			return 3;
		}
		nlohmann::json response;
		response["protocol"] = 1;
		response["cmd"] = "validate";
		response["ok"] = true;
		response["result"] = sceneInfo();
		std::cout << response.dump() << "\n";
		return 0;
	}

	if (!args_.headless && !args_.wantsScript() && !args_.wantsScreenshot()) {
		return runWindowed();
	}

	try {
		gpu_ = GpuContext::create(args_.backend);
	} catch (const std::exception& error) {
		logError(std::string("GPU initialization failed: ") + error.what());
		return 4;
	}

	try {
		renderer_.init(gpu_.device, gpu_.queue, wgpu::TextureFormat::RGBA8Unorm);
	} catch (const std::exception& error) {
		logError(std::string("renderer initialization failed: ") + error.what());
		return 4;
	}

	if (!args_.scenePath.empty()) {
		try {
			loadScene(args_.scenePath);
		} catch (const std::exception& error) {
			logError(std::string("scene loading failed: ") + error.what());
			return 3;
		}
	}
	tasksOverride_ = args_.tasksPath;
	applyInitialState();

	if (args_.wantsScreenshot()) {
		nlohmann::json payload;
		payload["path"] = args_.screenshotPath;
		payload["width"] = args_.width;
		payload["height"] = args_.height;
		if (args_.frame >= 0) {
			payload["frame"] = args_.frame;
		} else if (args_.time >= 0.0) {
			payload["time"] = args_.time;
		}
		try {
			const nlohmann::json result = commandScreenshot(payload);
			nlohmann::json response;
			response["protocol"] = 1;
			response["cmd"] = "screenshot";
			response["ok"] = true;
			response["result"] = result;
			std::cout << response.dump() << "\n";
			return 0;
		} catch (const CommandError& error) {
			nlohmann::json response;
			response["protocol"] = 1;
			response["cmd"] = "screenshot";
			response["ok"] = false;
			response["error"] = {{"code", error.code()}, {"message", error.what()}};
			std::cout << response.dump() << "\n";
			return error.code() == "NO_SCENE" ? 3 : 5;
		} catch (const std::exception& error) {
			logError(std::string("screenshot failed: ") + error.what());
			return 4;
		}
	}

	if (args_.wantsScript()) {
		printReadyLine();
		CommandBus bus(*this);
		JsonlDriver driver(bus, args_.strict, args_.quiet);
		if (args_.scriptPath == "-") {
			return driver.run(std::cin, std::cout);
		}
		std::ifstream script(args_.scriptPath);
		if (!script) {
			logError("cannot open script: " + args_.scriptPath);
			return 2;
		}
		return driver.run(script, std::cout);
	}

	if (args_.headless) {
		logError("nothing to do: pass --script, --screenshot or --validate-only");
		return 2;
	}

	logError("nothing to do: pass --script, --screenshot or --validate-only");
	return 2;
}

RenderFrame Application::buildRenderFrame(unsigned width, unsigned height) const {
	RenderFrame frame;
	if (!scene_ || !frameData_) {
		return frame;
	}
	const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 16.0f / 9.0f;
	const CameraFrame cameraFrame = Camera::compute(camera_, *frameData_, aspect, earthRadiusKm_);
	frame.viewProjection = cameraFrame.projection * cameraFrame.view;
	frame.cameraRight = cameraFrame.right;
	frame.cameraUp = cameraFrame.up;
	frame.cameraEye = cameraFrame.eye;

	// Keep billboards at a stable screen size while orbiting and zooming.
	const double cameraDistance = std::max(1.0, std::sqrt(
		(cameraFrame.eye.x - cameraFrame.focus.x) * (cameraFrame.eye.x - cameraFrame.focus.x)
		+ (cameraFrame.eye.y - cameraFrame.focus.y) * (cameraFrame.eye.y - cameraFrame.focus.y)
		+ (cameraFrame.eye.z - cameraFrame.focus.z) * (cameraFrame.eye.z - cameraFrame.focus.z)));
	const double fovRadians = camera_.fieldOfViewDeg * kPi / 180.0;
	const double pixelKm = 2.0 * std::tan(fovRadians * 0.5) * cameraDistance
	                     / static_cast<double>(std::max(1u, height));

	std::map<std::string, const SatelliteFrame*> byId;
	for (const SatelliteFrame& satellite : frameData_->satellites) {
		byId[satellite.id] = &satellite;
	}

	// Inter-satellite links, de-duplicated.
	if (layerEnabled("isl", true)) {
		for (const SatelliteFrame& satellite : frameData_->satellites) {
			for (const NeighborRef& neighbor : satellite.neighbors) {
				if (neighbor.id < satellite.id) {
					continue;
				}
				const auto found = byId.find(neighbor.id);
				if (found == byId.end()) {
					continue;
				}
				addLine(frame.lines, satellite.ecefKm, found->second->ecefKm, 0.30f, 0.62f, 1.0f, 0.55f);
			}
		}
	}

	// Visibility dome and buffer zone around the vessel.
	if (layerEnabled("dome", true)) {
		addCircleOnSphere(frame.lines, frameData_->vessel.ecefKm,
		                  elevationToAngularRadius(phiMaxDeg_, earthRadiusKm_, kVesselAltitudeKm),
		                  earthRadiusKm_ * 1.003, 128, 1.0f, 0.72f, 0.25f, 0.95f);
		addCircleOnSphere(frame.lines, frameData_->vessel.ecefKm,
		                  elevationToAngularRadius(phiBufferDeg_, earthRadiusKm_, kVesselAltitudeKm),
		                  earthRadiusKm_ * 1.003, 128, 1.0f, 0.85f, 0.45f, 0.65f);
	}

	// Great-circle route vessel -> observer.
	if (layerEnabled("route", true)) {
		addGreatCircleArc(frame.lines, frameData_->vessel.ecefKm, frameData_->observer.ecefKm,
		                  earthRadiusKm_ * 1.002, 128, 0.95f, 0.8f, 0.35f, 0.8f);
	}

	// Satellites.
	if (layerEnabled("satellites", true)) {
		for (const SatelliteFrame& satellite : frameData_->satellites) {
			PointInstance point;
			point.x = static_cast<float>(satellite.ecefKm.x);
			point.y = static_cast<float>(satellite.ecefKm.y);
			point.z = static_cast<float>(satellite.ecefKm.z);
			if (satellite.isAp) {
				point.r = 1.0f; point.g = 0.78f; point.b = 0.25f; point.a = 1.0f;
				point.sizeKm = static_cast<float>(10.0 * pixelKm);
			} else if (satellite.lifeRemainingS.has_value() && satellite.lifeRemainingS.value() > 0.0) {
				point.r = 0.35f; point.g = 0.9f; point.b = 0.45f; point.a = 1.0f;
				point.sizeKm = static_cast<float>(8.0 * pixelKm);
			} else {
				point.r = 0.55f; point.g = 0.68f; point.b = 0.85f; point.a = 1.0f;
				point.sizeKm = static_cast<float>(6.0 * pixelKm);
			}
			frame.points.push_back(point);
		}
	}

	// Terminals.
	PointInstance vessel;
	vessel.x = static_cast<float>(frameData_->vessel.ecefKm.x);
	vessel.y = static_cast<float>(frameData_->vessel.ecefKm.y);
	vessel.z = static_cast<float>(frameData_->vessel.ecefKm.z);
	vessel.r = 1.0f; vessel.g = 0.35f; vessel.b = 0.35f; vessel.a = 1.0f;
	vessel.sizeKm = static_cast<float>(10.0 * pixelKm);
	frame.points.push_back(vessel);

	PointInstance observer;
	observer.x = static_cast<float>(frameData_->observer.ecefKm.x);
	observer.y = static_cast<float>(frameData_->observer.ecefKm.y);
	observer.z = static_cast<float>(frameData_->observer.ecefKm.z);
	observer.r = 0.3f; observer.g = 0.85f; observer.b = 1.0f; observer.a = 1.0f;
	observer.sizeKm = static_cast<float>(8.0 * pixelKm);
	frame.points.push_back(observer);

	// Task pulses: interpolate the active hop at the current frame time.
	if (layerEnabled("tasks", true) && !tasks_.empty()) {
		std::map<int, std::vector<const TaskEvent*>> byTask;
		for (const TaskEvent& event : tasks_) {
			byTask[event.task].push_back(&event);
		}
		const double now = sceneTimeS_;
		for (const auto& entry : byTask) {
			const TaskEvent* last = nullptr;
			const TaskEvent* next = nullptr;
			for (const TaskEvent* event : entry.second) {
				if (event->t <= now + 1e-9) {
					last = event;
				} else {
					next = event;
					break;
				}
			}
			if (last == nullptr || last->event == "complete" || last->event == "fail") {
				continue;
			}

			Vec3 position;
			bool hasPosition = false;
			if (last->event == "hop_start" && !last->from.empty() && !last->to.empty()) {
				const Vec3 from = lookUpNode(*frameData_, last->from, scene_->terminalName, scene_->observerName);
				const Vec3 to = lookUpNode(*frameData_, last->to, scene_->terminalName, scene_->observerName);
				if (validNode(from) && validNode(to)) {
					double duration = last->durationS;
					if (duration <= 0.0 && next != nullptr) {
						duration = next->t - last->t;
					}
					const double visualDuration = std::max(duration, 0.6);
					const double progress = std::clamp((now - last->t) / visualDuration, 0.0, 1.0);
					position = lerp(from, to, progress);
					hasPosition = true;
				}
			}
			if (!hasPosition) {
				const std::string node = !last->node.empty() ? last->node : (!last->to.empty() ? last->to : last->from);
				position = lookUpNode(*frameData_, node, scene_->terminalName, scene_->observerName);
				hasPosition = validNode(position);
			}
			if (!hasPosition) {
				continue;
			}

			const std::array<float, 4> color = taskColor(last->type);
			PointInstance pulse;
			pulse.x = static_cast<float>(position.x);
			pulse.y = static_cast<float>(position.y);
			pulse.z = static_cast<float>(position.z);
			pulse.r = color[0];
			pulse.g = color[1];
			pulse.b = color[2];
			pulse.a = color[3];
			pulse.sizeKm = static_cast<float>(12.0 * pixelKm);
			frame.points.push_back(pulse);
		}
	}

	return frame;
}

nlohmann::json Application::commandOpen(const nlohmann::json& payload) {
	const std::string path = payload.value("scene", args_.scenePath);
	if (path.empty()) {
		throw CommandError("MISSING_FIELD", "'open' requires a 'scene' path or the --scene option");
	}
	if (payload.contains("tasks") && payload["tasks"].is_string()) {
		tasksOverride_ = payload["tasks"].get<std::string>();
	}
	loadScene(path);
	return sceneInfo();
}

nlohmann::json Application::commandSeek(const nlohmann::json& payload) {
	requireScene();
	if (payload.contains("frame")) {
		if (!payload["frame"].is_number_integer()) {
			throw CommandError("INVALID_FIELD", "'frame' must be an integer");
		}
		setFrame(payload["frame"].get<int>());
	} else if (payload.contains("time")) {
		if (!payload["time"].is_number()) {
			throw CommandError("INVALID_FIELD", "'time' must be a number");
		}
		seekToTime(payload["time"].get<double>());
	} else {
		throw CommandError("MISSING_FIELD", "'seek' requires 'frame' or 'time'");
	}
	return nlohmann::json{{"frame", currentFrame_}, {"time_s", sceneTimeS_}};
}

nlohmann::json Application::commandStep(const nlohmann::json& payload) {
	requireScene();
	const int delta = payload.value("delta", 1);
	setFrame(currentFrame_ + delta);
	return nlohmann::json{{"frame", currentFrame_}, {"time_s", scene_->frameTime(currentFrame_)}};
}

nlohmann::json Application::commandPlay(const nlohmann::json& payload) {
	(void)payload;
	requireScene();
	playing_ = true;
	return nlohmann::json{{"playing", true}};
}

nlohmann::json Application::commandPause(const nlohmann::json& payload) {
	(void)payload;
	playing_ = false;
	return nlohmann::json{{"playing", false}};
}

nlohmann::json Application::commandSpeed(const nlohmann::json& payload) {
	if (!payload.contains("value") || !payload["value"].is_number()) {
		throw CommandError("MISSING_FIELD", "'speed' requires a numeric 'value'");
	}
	const double value = payload["value"].get<double>();
	if (value <= 0.0) {
		throw CommandError("INVALID_FIELD", "'speed' must be positive");
	}
	speed_ = value;
	return nlohmann::json{{"speed", speed_}};
}

nlohmann::json Application::commandCamera(const nlohmann::json& payload) {
	const std::string previousMode = camera_.mode;
	if (payload.contains("mode") && payload["mode"].is_string()) {
		camera_.mode = payload["mode"].get<std::string>();
	}
	if (payload.contains("target") && payload["target"].is_string()) {
		camera_.target = payload["target"].get<std::string>();
	}
	if (payload.contains("distance_km") && payload["distance_km"].is_number()) {
		camera_.distanceKm = payload["distance_km"].get<double>();
	} else if (camera_.mode != previousMode) {
		if (camera_.mode == "vessel" || camera_.mode == "topdown") {
			camera_.distanceKm = 4500.0;
		} else if (camera_.mode == "satellite") {
			camera_.distanceKm = 1500.0;
		}
	}
	if (payload.contains("yaw_deg") && payload["yaw_deg"].is_number()) {
		camera_.yawDeg = payload["yaw_deg"].get<double>();
	}
	if (payload.contains("pitch_deg") && payload["pitch_deg"].is_number()) {
		camera_.pitchDeg = payload["pitch_deg"].get<double>();
	}
	nlohmann::json result;
	result["mode"] = camera_.mode;
	result["target"] = camera_.target;
	result["distance_km"] = camera_.distanceKm;
	return result;
}

nlohmann::json Application::commandLayer(const nlohmann::json& payload) {
	if (!payload.contains("name") || !payload["name"].is_string()) {
		throw CommandError("MISSING_FIELD", "'layer' requires a 'name'");
	}
	if (!payload.contains("visible") || !payload["visible"].is_boolean()) {
		throw CommandError("MISSING_FIELD", "'layer' requires a boolean 'visible'");
	}
	const std::string name = payload["name"].get<std::string>();
	const bool visible = payload["visible"].get<bool>();
	layers_[name] = visible;
	if (name == "grid") {
		buildEarthGrid();
	}
	return nlohmann::json{{"name", name}, {"visible", visible}};
}

nlohmann::json Application::commandSelect(const nlohmann::json& payload) {
	if (!payload.contains("entity") || !payload["entity"].is_string()) {
		throw CommandError("MISSING_FIELD", "'select' requires an 'entity'");
	}
	selected_ = payload["entity"].get<std::string>();
	return nlohmann::json{{"entity", selected_}};
}

nlohmann::json Application::commandReset(const nlohmann::json& payload) {
	(void)payload;
	if (scene_) {
		setFrame(0);
	}
	playing_ = false;
	return nlohmann::json{{"frame", currentFrame_}, {"playing", false}};
}

nlohmann::json Application::commandState(const nlohmann::json& payload) {
	(void)payload;
	nlohmann::json result;
	result["scene"] = scene_ ? scene_->manifestPath.string() : "";
	result["frame"] = currentFrame_;
	result["time_s"] = scene_ ? sceneTimeS_ : 0.0;
	result["frame_count"] = scene_ ? scene_->frameCount : 0;
	result["playing"] = playing_;
	result["speed"] = speed_;
	result["camera"] = {
		{"mode", camera_.mode},
		{"target", camera_.target},
		{"distance_km", camera_.distanceKm},
	};
	result["layers"] = layers_;
	result["selected"] = selected_;
	result["backend"] = gpu_.backendName;
	result["adapter"] = gpu_.adapterName;
	if (frameData_) {
		std::size_t accessPoints = 0;
		for (const SatelliteFrame& satellite : frameData_->satellites) {
			if (satellite.isAp) {
				++accessPoints;
			}
		}
		result["satellites"] = frameData_->satellites.size();
		result["access_points"] = accessPoints;
	}
	result["tasks"] = tasks_.size();
	return result;
}

nlohmann::json Application::commandScreenshot(const nlohmann::json& payload) {
	requireScene();

	if (payload.contains("frame")) {
		if (!payload["frame"].is_number_integer()) {
			throw CommandError("INVALID_FIELD", "'frame' must be an integer");
		}
		setFrame(payload["frame"].get<int>());
	} else if (payload.contains("time")) {
		if (!payload["time"].is_number()) {
			throw CommandError("INVALID_FIELD", "'time' must be a number");
		}
		seekToTime(payload["time"].get<double>());
	}

	const unsigned width = payload.value("width", static_cast<unsigned>(args_.width));
	const unsigned height = payload.value("height", static_cast<unsigned>(args_.height));
	if (width == 0 || height == 0) {
		throw CommandError("INVALID_FIELD", "screenshot size must be positive");
	}

	const std::string candidate = payload.value("path", std::string("screenshot.png"));
	const std::filesystem::path outputPath = resolveOutputPath(args_.outputDir, candidate);

	renderer_.update(buildRenderFrame(width, height));
	const ScreenshotResult result = ScreenshotService::capture(gpu_, renderer_, width, height, outputPath);

	nlohmann::json response;
	response["path"] = result.path.string();
	response["width"] = result.width;
	response["height"] = result.height;
	response["bytes"] = result.bytes;
	response["sha256"] = result.sha256;
	response["mean"] = result.stats.mean;
	response["std"] = result.stats.stddev;
	response["nonblank"] = result.stats.nonblank;
	response["frame"] = currentFrame_;
	response["time_s"] = sceneTimeS_;
	response["lines"] = renderer_.lastLineCount();
	response["points"] = renderer_.lastPointCount();
	return response;
}

nlohmann::json Application::commandValidate(const nlohmann::json& payload) {
	(void)payload;
	if (!scene_) {
		throw CommandError("NO_SCENE", "no scene loaded; run 'open' first");
	}
	return sceneInfo();
}

nlohmann::json Application::commandQuit(const nlohmann::json& payload) {
	(void)payload;
	quit_ = true;
	return nlohmann::json::object();
}

int Application::runWindowed() {
#ifdef SECGUI_WINDOW
	if (args_.scenePath.empty()) {
		logError("windowed mode requires --scene <manifest>");
		return 2;
	}

	glfwSetErrorCallback([](int code, const char* description) {
		logError("glfw error " + std::to_string(code) + ": "
		         + (description != nullptr ? description : "unknown"));
	});
	if (!glfwInit()) {
		logError("failed to initialize GLFW");
		return 4;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	const int initialWidth = args_.width > 0 ? args_.width : 1280;
	const int initialHeight = args_.height > 0 ? args_.height : 720;
	GLFWwindow* window = glfwCreateWindow(initialWidth, initialHeight,
	                                      "SECMotionModel GUI", nullptr, nullptr);
	if (window == nullptr) {
		logError("failed to create the GLFW window");
		glfwTerminate();
		return 4;
	}

	struct WindowCleanup {
		GLFWwindow* window = nullptr;
		GpuContext* gpu = nullptr;
		bool imguiReady = false;
		~WindowCleanup() {
			if (imguiReady) {
				ImGui_ImplWGPU_Shutdown();
				ImGui_ImplGlfw_Shutdown();
				ImGui::DestroyContext();
			}
			if (gpu != nullptr && gpu->surface) {
				gpu->surface.unconfigure();
				gpu->surface.release();
				gpu->surface = wgpu::Surface();
			}
			if (window != nullptr) {
				glfwDestroyWindow(window);
			}
			glfwTerminate();
		}
	} cleanup{window, &gpu_};

	try {
		gpu_ = GpuContext::createForWindow(args_.backend, window);
	} catch (const std::exception& error) {
		logError(std::string("GPU initialization failed: ") + error.what());
		return 4;
	}

	wgpu::SurfaceCapabilities capabilities{};
	if (gpu_.surface.getCapabilities(gpu_.adapter, &capabilities) != wgpu::Status::Success
	    || capabilities.formatCount == 0) {
		logError("the window surface exposes no texture format");
		return 4;
	}
	const wgpu::TextureFormat format = capabilities.formats[0];

	unsigned width = static_cast<unsigned>(initialWidth);
	unsigned height = static_cast<unsigned>(initialHeight);

	const auto configureSurface = [&](unsigned surfaceWidth, unsigned surfaceHeight) {
		wgpu::SurfaceConfiguration configuration{};
		configuration.device = gpu_.device;
		configuration.format = format;
		configuration.usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_RenderAttachment);
		configuration.width = surfaceWidth;
		configuration.height = surfaceHeight;
		configuration.viewFormatCount = 0;
		configuration.viewFormats = nullptr;
		configuration.alphaMode = wgpu::CompositeAlphaMode::Auto;
		configuration.presentMode = wgpu::PresentMode::Fifo;
		gpu_.surface.configure(configuration);
	};

	wgpu::Texture depthTexture;
	wgpu::TextureView depthView;
	const auto createDepth = [&](unsigned surfaceWidth, unsigned surfaceHeight) {
		if (depthView) depthView.release();
		if (depthTexture) depthTexture.release();
		wgpu::TextureDescriptor descriptor{};
		descriptor.label = wgpu::StringView("window depth");
		descriptor.size = wgpu::Extent3D{surfaceWidth, surfaceHeight, 1};
		descriptor.mipLevelCount = 1;
		descriptor.sampleCount = 1;
		descriptor.dimension = wgpu::TextureDimension::_2D;
		descriptor.format = wgpu::TextureFormat::Depth24Plus;
		descriptor.usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_RenderAttachment);
		depthTexture = gpu_.device.createTexture(descriptor);
		depthView = depthTexture.createView();
	};

	configureSurface(width, height);
	createDepth(width, height);

	try {
		renderer_.init(gpu_.device, gpu_.queue, format);
	} catch (const std::exception& error) {
		logError(std::string("renderer initialization failed: ") + error.what());
		return 4;
	}

	try {
		loadScene(args_.scenePath);
	} catch (const std::exception& error) {
		logError(std::string("scene loading failed: ") + error.what());
		return 3;
	}
	if (args_.cameraMode.empty()) {
		camera_.mode = "vessel";
	}
	applyInitialState();
	buildEarthGrid();
	if (!args_.recordScriptPath.empty()) {
		recordCommand("open", nlohmann::json{{"scene", args_.scenePath}});
		recordCommand("camera", nlohmann::json{
			{"mode", camera_.mode},
			{"target", camera_.target},
			{"distance_km", camera_.distanceKm},
			{"yaw_deg", camera_.yawDeg},
			{"pitch_deg", camera_.pitchDeg},
		});
		recordCommand("speed", nlohmann::json{{"value", speed_}});
		for (const char* name : {"grid", "isl", "dome", "route", "satellites", "tasks"}) {
			recordCommand("layer", nlohmann::json{
				{"name", name}, {"visible", layerEnabled(name, true)}});
		}
		if (sceneTimeS_ > 0.0) {
			recordCommand("seek", nlohmann::json{{"time", sceneTimeS_}});
		}
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGui::GetStyle().WindowRounding = 8.0f;
	ImGui::GetStyle().FrameRounding = 4.0f;

	if (!ImGui_ImplGlfw_InitForOther(window, true)) {
		logError("failed to initialize the Dear ImGui GLFW backend");
		return 4;
	}
	ImGui_ImplWGPU_InitInfo imguiInfo{};
	imguiInfo.Device = gpu_.device;
	imguiInfo.NumFramesInFlight = 2;
	imguiInfo.RenderTargetFormat = format;
	imguiInfo.DepthStencilFormat = wgpu::TextureFormat::Undefined;
	if (!ImGui_ImplWGPU_Init(&imguiInfo)) {
		ImGui_ImplGlfw_Shutdown();
		logError("failed to initialize the Dear ImGui WebGPU backend");
		return 4;
	}
	cleanup.imguiReady = true;

	bool showLayers = true;
	bool showTelemetry = true;
	bool showPalette = false;
	char paletteBuffer[512] = "{\"cmd\":\"state\"}";
	std::string paletteResult;
	std::string statusMessage;

	bool dragging = false;
	bool clickCandidate = false;
	bool cameraMoved = false;
	double lastMouseX = 0.0;
	double lastMouseY = 0.0;
	double lastFrameTime = glfwGetTime();

	const auto runCommand = [&](const std::string& command, const nlohmann::json& payload) {
		try {
			CommandBus bus(*this);
			bus.dispatch(command, payload);
			recordCommand(command, payload);
		} catch (const std::exception& error) {
			statusMessage = command + ": " + error.what();
		}
	};
	const auto takeScreenshot = [&]() {
		runCommand("screenshot", nlohmann::json{
			{"path", "gui_frame_" + std::to_string(currentFrame_) + ".png"},
			{"width", width},
			{"height", height},
		});
		statusMessage = "screenshot saved to " + args_.outputDir;
	};

	logInfo("window ready: " + std::to_string(width) + "x" + std::to_string(height)
	        + " backend=" + gpu_.backendName + " adapter=" + gpu_.adapterName);

	unsigned long long renderedFrames = 0;
	while (!glfwWindowShouldClose(window) && !quit_) {
		glfwPollEvents();

		const double now = glfwGetTime();
		const double delta = std::clamp(now - lastFrameTime, 0.0, 0.1);
		lastFrameTime = now;

		int framebufferWidth = 0;
		int framebufferHeight = 0;
		glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
		if (framebufferWidth <= 0 || framebufferHeight <= 0) {
			glfwWaitEventsTimeout(0.05);
			continue;
		}
		if (static_cast<unsigned>(framebufferWidth) != width
		    || static_cast<unsigned>(framebufferHeight) != height) {
			width = static_cast<unsigned>(framebufferWidth);
			height = static_cast<unsigned>(framebufferHeight);
			configureSurface(width, height);
			createDepth(width, height);
		}

		advancePlayback(delta);

		RenderFrame sceneFrame = buildRenderFrame(width, height);

		if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
			camera_.distanceKm = std::clamp(
				camera_.distanceKm * std::pow(0.88, static_cast<double>(io.MouseWheel)),
				500.0, 60000.0);
			runCommand("camera", nlohmann::json{
				{"mode", camera_.mode},
				{"target", camera_.target},
				{"distance_km", camera_.distanceKm},
			});
		}
		if (!io.WantCaptureMouse && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
			double mouseX = 0.0;
			double mouseY = 0.0;
			glfwGetCursorPos(window, &mouseX, &mouseY);
			if (!dragging) {
				dragging = true;
				clickCandidate = true;
				lastMouseX = mouseX;
				lastMouseY = mouseY;
			} else {
				const double deltaX = mouseX - lastMouseX;
				const double deltaY = mouseY - lastMouseY;
				if (std::abs(deltaX) + std::abs(deltaY) > 2.0) {
					clickCandidate = false;
				}
				camera_.yawDeg -= deltaX * 0.3;
				camera_.pitchDeg = std::clamp(camera_.pitchDeg + deltaY * 0.3, 2.0, 88.0);
				cameraMoved = true;
				lastMouseX = mouseX;
				lastMouseY = mouseY;
			}
		} else if (dragging) {
			if (clickCandidate && scene_ && frameData_) {
				double mouseX = 0.0;
				double mouseY = 0.0;
				glfwGetCursorPos(window, &mouseX, &mouseY);
				std::optional<std::pair<std::string, double>> best;
				const auto consider = [&](const std::string& id, const Vec3& position) {
					if (earthOccludes(sceneFrame.cameraEye, position, earthRadiusKm_ * 1.01)) {
						return;
					}
					const ScreenPoint screen = projectToScreen(sceneFrame, position, width, height);
					if (!screen.visible) {
						return;
					}
					const double distance = std::hypot(
						static_cast<double>(screen.x) - mouseX,
						static_cast<double>(screen.y) - mouseY);
					if (distance <= 22.0 && (!best || distance < best->second)) {
						best = std::make_pair(id, distance);
					}
				};
				for (const SatelliteFrame& satellite : frameData_->satellites) {
					consider(satellite.id, satellite.ecefKm);
				}
				consider(frameData_->vessel.name, frameData_->vessel.ecefKm);
				consider(frameData_->observer.name, frameData_->observer.ecefKm);
				if (best) {
					runCommand("select", nlohmann::json{{"entity", best->first}});
				}
			}
			dragging = false;
			clickCandidate = false;
			if (cameraMoved) {
				runCommand("camera", nlohmann::json{
					{"mode", camera_.mode},
					{"target", camera_.target},
					{"yaw_deg", camera_.yawDeg},
					{"pitch_deg", camera_.pitchDeg},
					{"distance_km", camera_.distanceKm},
				});
				cameraMoved = false;
			}
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplWGPU_NewFrame();
		ImGui::NewFrame();

		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K)) {
			showPalette = !showPalette;
		}
		if (!io.WantTextInput) {
			if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
				runCommand(playing_ ? "pause" : "play", nlohmann::json::object());
			}
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
				runCommand("step", nlohmann::json{{"delta", -1}});
			}
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
				runCommand("step", nlohmann::json{{"delta", 1}});
			}
			if (ImGui::IsKeyPressed(ImGuiKey_L)) {
				showLayers = !showLayers;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_T)) {
				showTelemetry = !showTelemetry;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_R)) {
				runCommand("reset", nlohmann::json::object());
			}
			if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
				runCommand("speed", nlohmann::json{{"value", std::max(0.25, speed_ / 1.5)}});
			}
			if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
				runCommand("speed", nlohmann::json{{"value", std::min(8.0, speed_ * 1.5)}});
			}
			const char* cameraShortcuts[] = {"orbit", "vessel", "topdown", "satellite", "free"};
			const ImGuiKey cameraKeys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5};
			for (int i = 0; i < 5; ++i) {
				if (ImGui::IsKeyPressed(cameraKeys[i])) {
					runCommand("camera", nlohmann::json{
						{"mode", cameraShortcuts[i]}, {"target", camera_.target}});
				}
			}
			const std::pair<const char*, ImGuiKey> layerShortcuts[] = {
				{"grid", ImGuiKey_G},
				{"isl", ImGuiKey_I},
				{"dome", ImGuiKey_D},
				{"route", ImGuiKey_O},
				{"tasks", ImGuiKey_K},
			};
			for (const auto& entry : layerShortcuts) {
				if (ImGui::IsKeyPressed(entry.second)) {
					runCommand("layer", nlohmann::json{
						{"name", entry.first},
						{"visible", !layerEnabled(entry.first, true)},
					});
				}
			}
			if (ImGui::IsKeyPressed(ImGuiKey_F12)) {
				takeScreenshot();
			}
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) {
			quit_ = true;
		}

		std::size_t accessPoints = 0;
		if (frameData_) {
			for (const SatelliteFrame& satellite : frameData_->satellites) {
				if (satellite.isAp) {
					++accessPoints;
				}
			}
		}

		ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("SECMotionModel GUI", nullptr,
		                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
			ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.0f, 1.0f), "SECMotionModel GUI");
			ImGui::Separator();
			ImGui::Text("Scene   %s", scene_ ? scene_->manifestPath.filename().string().c_str() : "-");
			ImGui::Text("Frame   %d / %d", currentFrame_,
			            scene_ ? scene_->frameCount - 1 : 0);
			ImGui::Text("Time    %.2f / %.2f s", sceneTimeS_,
			            scene_ ? scene_->durationS : 0.0);
			ImGui::Text("FPS     %.0f", io.Framerate);
			ImGui::Text("Sats    %zu (AP %zu)", frameData_ ? frameData_->satellites.size() : 0,
			            accessPoints);
			ImGui::Text("Draw    %zu lines, %zu points", renderer_.lastLineCount(),
			            renderer_.lastPointCount());
			ImGui::Text("Tasks   %zu", tasks_.size());
			ImGui::Text("Camera  %s -> %s", camera_.mode.c_str(), camera_.target.c_str());
			ImGui::Text("GPU     %s / %s", gpu_.adapterName.c_str(), gpu_.backendName.c_str());
			ImGui::Separator();
			if (ImGui::Button("Screenshot [F12]")) {
				takeScreenshot();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset [R]")) {
				runCommand("reset", nlohmann::json::object());
			}
			if (!statusMessage.empty()) {
				ImGui::Separator();
				ImGui::TextWrapped("%s", statusMessage.c_str());
			}
		}
		ImGui::End();

		if (showLayers) {
			ImGui::SetNextWindowPos(ImVec2(static_cast<float>(width) - 250.0f, 16.0f),
			                        ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Layers", &showLayers, ImGuiWindowFlags_AlwaysAutoResize)) {
				const std::pair<const char*, const char*> layerNames[] = {
					{"grid", "Earth grid"},
					{"isl", "Inter-satellite links"},
					{"dome", "Visibility dome"},
					{"route", "Vessel route"},
					{"satellites", "Satellites"},
					{"tasks", "Task pulses"},
				};
				for (const auto& entry : layerNames) {
					bool visible = layerEnabled(entry.first, true);
					if (ImGui::Checkbox(entry.second, &visible)) {
						runCommand("layer", nlohmann::json{
							{"name", entry.first}, {"visible", visible}});
					}
				}
				ImGui::Separator();
				ImGui::TextDisabled("Shortcuts");
				ImGui::Text("Space  play/pause");
				ImGui::Text("Left/Right  step");
				ImGui::Text("L layers  T telemetry");
				ImGui::Text("Ctrl+K command palette");
				ImGui::Text("F12 screenshot");
				ImGui::Text("Drag orbit, wheel zoom");
			}
			ImGui::End();
		}

		if (showTelemetry) {
			ImGui::SetNextWindowPos(ImVec2(16.0f, 280.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Telemetry", &showTelemetry, ImGuiWindowFlags_AlwaysAutoResize)) {
				const SatelliteFrame* satellite = nullptr;
				if (frameData_) {
					for (const SatelliteFrame& candidate : frameData_->satellites) {
						if (candidate.id == selected_) {
							satellite = &candidate;
							break;
						}
					}
				}
				if (satellite != nullptr) {
					ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", satellite->id.c_str());
					ImGui::Text("ECEF    %.1f, %.1f, %.1f km",
					            satellite->ecefKm.x, satellite->ecefKm.y, satellite->ecefKm.z);
					ImGui::Text("Access point  %s", satellite->isAp ? "yes" : "no");
					if (!satellite->terminalRef.empty()) {
						ImGui::Text("Terminal  %s", satellite->terminalRef.c_str());
					}
					if (satellite->elevDeg.has_value()) {
						ImGui::Text("Elevation  %.1f deg", satellite->elevDeg.value());
					}
					if (satellite->rangeKm.has_value()) {
						ImGui::Text("Range  %.1f km", satellite->rangeKm.value());
					}
					if (satellite->lifeRemainingS.has_value()) {
						ImGui::Text("Life remaining  %.1f s", satellite->lifeRemainingS.value());
					}
					ImGui::Text("Neighbors  %zu", satellite->neighbors.size());
					if (ImGui::Button("Follow with camera")) {
						runCommand("camera", nlohmann::json{
							{"mode", "satellite"}, {"target", satellite->id}});
					}
				} else if (frameData_ && selected_ == frameData_->vessel.name) {
					ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "%s",
					                   frameData_->vessel.name.c_str());
					ImGui::Text("Lat/Lon  %.4f, %.4f", frameData_->vessel.lat, frameData_->vessel.lon);
					ImGui::Text("ECEF  %.1f, %.1f, %.1f km", frameData_->vessel.ecefKm.x,
					            frameData_->vessel.ecefKm.y, frameData_->vessel.ecefKm.z);
					if (ImGui::Button("Follow with camera")) {
						runCommand("camera", nlohmann::json{
							{"mode", "vessel"}, {"target", frameData_->vessel.name}});
					}
				} else if (frameData_ && selected_ == frameData_->observer.name) {
					ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "%s",
					                   frameData_->observer.name.c_str());
					ImGui::Text("Lat/Lon  %.4f, %.4f", frameData_->observer.lat, frameData_->observer.lon);
					ImGui::Text("ECEF  %.1f, %.1f, %.1f km", frameData_->observer.ecefKm.x,
					            frameData_->observer.ecefKm.y, frameData_->observer.ecefKm.z);
				} else {
					ImGui::TextWrapped("Click a satellite, the vessel or the observer to inspect it.");
				}
				ImGui::Separator();
				const char* cameraModes[] = {"orbit", "vessel", "topdown", "satellite", "free"};
				int cameraIndex = 0;
				for (int i = 0; i < 5; ++i) {
					if (camera_.mode == cameraModes[i]) {
						cameraIndex = i;
					}
				}
				if (ImGui::Combo("Camera mode", &cameraIndex, cameraModes, 5)) {
					runCommand("camera", nlohmann::json{
						{"mode", cameraModes[cameraIndex]}, {"target", camera_.target}});
				}
			}
			ImGui::End();
		}

		ImGui::SetNextWindowPos(
			ImVec2(16.0f, io.DisplaySize.y - 112.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(
			ImVec2(std::max(520.0f, io.DisplaySize.x - 32.0f), 96.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoCollapse)) {
			if (ImGui::Button(playing_ ? "Pause" : "Play")) {
				runCommand(playing_ ? "pause" : "play", nlohmann::json::object());
			}
			ImGui::SameLine();
			if (ImGui::Button("|<")) {
				runCommand("seek", nlohmann::json{{"frame", 0}});
			}
			ImGui::SameLine();
			if (ImGui::Button("<")) {
				runCommand("step", nlohmann::json{{"delta", -1}});
			}
			ImGui::SameLine();
			if (ImGui::Button(">")) {
				runCommand("step", nlohmann::json{{"delta", 1}});
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(150.0f);
			float speedValue = static_cast<float>(speed_);
			if (ImGui::SliderFloat("##speed", &speedValue, 0.25f, 8.0f, "%.2fx",
			                       ImGuiSliderFlags_Logarithmic)) {
				runCommand("speed", nlohmann::json{{"value", speedValue}});
			}
			ImGui::SameLine();
			ImGui::Text("Speed");
			ImGui::SameLine();
			ImGui::Text("%.2f / %.2f s", sceneTimeS_, scene_ ? scene_->durationS : 0.0);
			float sliderTime = static_cast<float>(sceneTimeS_);
			const float duration = scene_ ? static_cast<float>(scene_->durationS) : 1.0f;
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderFloat("##timeline", &sliderTime, 0.0f, std::max(duration, 0.001f),
			                       "%.2f s")) {
				seekToTime(static_cast<double>(sliderTime));
			}
		}
		ImGui::End();

		if (showPalette) {
			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.35f),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::Begin("Command palette", &showPalette,
			                 ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextDisabled("JSON command (Ctrl+K to toggle)");
				ImGui::SetNextItemWidth(560.0f);
				const bool submitted = ImGui::InputText(
					"##palette", paletteBuffer, sizeof(paletteBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::Button("Run") || submitted) {
					try {
						const nlohmann::json document = nlohmann::json::parse(paletteBuffer);
						if (!document.contains("cmd") || !document["cmd"].is_string()) {
							throw std::runtime_error("missing string field 'cmd'");
						}
						nlohmann::json payload = document;
						payload.erase("cmd");
						CommandBus bus(*this);
						const std::string command = document["cmd"].get<std::string>();
						paletteResult = bus.dispatch(command, payload).dump();
						recordCommand(command, payload);
					} catch (const std::exception& error) {
						paletteResult = std::string("error: ") + error.what();
					}
				}
				if (!paletteResult.empty()) {
					ImGui::Separator();
					ImGui::TextWrapped("%s", paletteResult.c_str());
				}
			}
			ImGui::End();
		}

		if (scene_ && frameData_) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const auto drawEntityLabel = [&](const std::string& name, const Vec3& position,
			                                 ImU32 color) {
				if (earthOccludes(sceneFrame.cameraEye, position, earthRadiusKm_ * 1.01)) {
					return;
				}
				drawOverlayLabel(drawList, projectToScreen(sceneFrame, position, width, height),
				                 color, name);
			};
			drawEntityLabel(frameData_->vessel.name, frameData_->vessel.ecefKm,
			                IM_COL32(90, 220, 255, 255));
			drawEntityLabel(frameData_->observer.name, frameData_->observer.ecefKm,
			                IM_COL32(120, 170, 255, 255));
			int extraLabels = 0;
			for (const SatelliteFrame& satellite : frameData_->satellites) {
				const bool isSelected = satellite.id == selected_;
				if (!isSelected && !satellite.isAp) {
					if (extraLabels >= 24) {
						continue;
					}
					++extraLabels;
				}
				const ImU32 color = isSelected
					? IM_COL32(255, 230, 90, 255)
					: (satellite.isAp ? IM_COL32(255, 190, 70, 255) : IM_COL32(170, 195, 225, 210));
				drawEntityLabel(satellite.id, satellite.ecefKm, color);
			}
		}

		ImGui::Render();

		sceneFrame = buildRenderFrame(width, height);
		renderer_.update(sceneFrame);

		wgpu::SurfaceTexture surfaceTexture{};
		gpu_.surface.getCurrentTexture(&surfaceTexture);
		if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
		    && surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
			if (surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated
			    || surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Lost) {
				configureSurface(width, height);
				createDepth(width, height);
			}
			glfwWaitEventsTimeout(0.05);
			continue;
		}

		wgpu::Texture surfaceTextureHandle(surfaceTexture.texture);
		const wgpu::TextureView surfaceView = surfaceTextureHandle.createView();
		const wgpu::CommandEncoder encoder = gpu_.device.createCommandEncoder();

		renderer_.render(encoder, surfaceView, depthView, width, height);

		wgpu::RenderPassColorAttachment overlayAttachment{};
		overlayAttachment.view = surfaceView;
		overlayAttachment.resolveTarget = nullptr;
		overlayAttachment.loadOp = wgpu::LoadOp::Load;
		overlayAttachment.storeOp = wgpu::StoreOp::Store;
		overlayAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};
		overlayAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

		wgpu::RenderPassDescriptor overlayDescriptor{};
		overlayDescriptor.label = wgpu::StringView("imgui pass");
		overlayDescriptor.colorAttachmentCount = 1;
		overlayDescriptor.colorAttachments = &overlayAttachment;
		overlayDescriptor.depthStencilAttachment = nullptr;
		overlayDescriptor.occlusionQuerySet = nullptr;
		overlayDescriptor.timestampWrites = nullptr;

		const auto overlayPass = encoder.beginRenderPass(overlayDescriptor);
		ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), overlayPass);
		overlayPass.end();

		const wgpu::CommandBuffer commands = encoder.finish();
		gpu_.queue.submit(commands);
		gpu_.surface.present();

		surfaceView.release();
		surfaceTextureHandle.release();
		commands.release();
		encoder.release();

		++renderedFrames;
		if (args_.windowFrames > 0
		    && renderedFrames >= static_cast<unsigned long long>(args_.windowFrames)) {
			break;
		}
	}

	if (depthView) depthView.release();
	if (depthTexture) depthTexture.release();
	return 0;
#else
	logError("windowed mode is not available: this build was configured with SECGUI_BUILD_WINDOW=OFF");
	return 2;
#endif
}

} // namespace secgui
