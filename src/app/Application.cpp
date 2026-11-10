#include "app/Application.hpp"

#include "cli/JsonlDriver.hpp"
#include "commands/CommandBus.hpp"
#include "screenshot/Screenshot.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>

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

} // namespace

Application::Application(Args args)
	: args_(std::move(args)) {}

void Application::loadScene(const std::filesystem::path& path) {
	scene_ = SceneLoader::load(path);
	earthRadiusKm_ = scene_->earthRadiusKm;
	phiMaxDeg_ = scene_->phiMaxDeg;
	phiBufferDeg_ = scene_->phiBufferDeg;
	currentFrame_ = 0;
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
	renderer_.setEarthGrid(makeEarthGrid(earthRadiusKm_));
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
}

void Application::setFrame(int frame) {
	requireScene();
	if (frame < 0 || frame >= scene_->frameCount) {
		throw CommandError("OUT_OF_RANGE",
			"frame " + std::to_string(frame) + " is outside [0, "
			+ std::to_string(scene_->frameCount - 1) + "]");
	}
	currentFrame_ = frame;
	reloadFrame();
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
		payload["frame"] = args_.frame >= 0 ? args_.frame : 0;
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

	logError("windowed mode is not implemented yet (Phase 3); use --headless");
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

	std::map<std::string, const SatelliteFrame*> byId;
	for (const SatelliteFrame& satellite : frameData_->satellites) {
		byId[satellite.id] = &satellite;
	}

	// Inter-satellite links, de-duplicated.
	for (const SatelliteFrame& satellite : frameData_->satellites) {
		for (const NeighborRef& neighbor : satellite.neighbors) {
			if (neighbor.id < satellite.id) {
				continue;
			}
			const auto found = byId.find(neighbor.id);
			if (found == byId.end()) {
				continue;
			}
			addLine(frame.lines, satellite.ecefKm, found->second->ecefKm, 0.25f, 0.55f, 0.95f, 0.35f);
		}
	}

	// Visibility dome and buffer zone around the vessel.
	addCircleOnSphere(frame.lines, frameData_->vessel.ecefKm,
	                  elevationToAngularRadius(phiMaxDeg_, earthRadiusKm_, kVesselAltitudeKm),
	                  earthRadiusKm_ * 1.003, 128, 1.0f, 0.72f, 0.25f, 0.85f);
	addCircleOnSphere(frame.lines, frameData_->vessel.ecefKm,
	                  elevationToAngularRadius(phiBufferDeg_, earthRadiusKm_, kVesselAltitudeKm),
	                  earthRadiusKm_ * 1.003, 128, 1.0f, 0.85f, 0.45f, 0.45f);

	// Great-circle route vessel -> observer.
	addGreatCircleArc(frame.lines, frameData_->vessel.ecefKm, frameData_->observer.ecefKm,
	                  earthRadiusKm_ * 1.002, 128, 0.95f, 0.8f, 0.35f, 0.6f);

	// Satellites.
	for (const SatelliteFrame& satellite : frameData_->satellites) {
		PointInstance point;
		point.x = static_cast<float>(satellite.ecefKm.x);
		point.y = static_cast<float>(satellite.ecefKm.y);
		point.z = static_cast<float>(satellite.ecefKm.z);
		if (satellite.isAp) {
			point.r = 1.0f; point.g = 0.78f; point.b = 0.25f; point.a = 1.0f;
			point.sizeKm = 190.0f;
		} else if (satellite.lifeRemainingS.has_value() && satellite.lifeRemainingS.value() > 0.0) {
			point.r = 0.35f; point.g = 0.9f; point.b = 0.45f; point.a = 1.0f;
			point.sizeKm = 120.0f;
		} else {
			point.r = 0.55f; point.g = 0.68f; point.b = 0.85f; point.a = 1.0f;
			point.sizeKm = 95.0f;
		}
		frame.points.push_back(point);
	}

	// Terminals.
	PointInstance vessel;
	vessel.x = static_cast<float>(frameData_->vessel.ecefKm.x);
	vessel.y = static_cast<float>(frameData_->vessel.ecefKm.y);
	vessel.z = static_cast<float>(frameData_->vessel.ecefKm.z);
	vessel.r = 1.0f; vessel.g = 0.35f; vessel.b = 0.35f; vessel.a = 1.0f;
	vessel.sizeKm = 230.0f;
	frame.points.push_back(vessel);

	PointInstance observer;
	observer.x = static_cast<float>(frameData_->observer.ecefKm.x);
	observer.y = static_cast<float>(frameData_->observer.ecefKm.y);
	observer.z = static_cast<float>(frameData_->observer.ecefKm.z);
	observer.r = 0.3f; observer.g = 0.85f; observer.b = 1.0f; observer.a = 1.0f;
	observer.sizeKm = 200.0f;
	frame.points.push_back(observer);

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
	int frame = currentFrame_;
	if (payload.contains("frame")) {
		if (!payload["frame"].is_number_integer()) {
			throw CommandError("INVALID_FIELD", "'frame' must be an integer");
		}
		frame = payload["frame"].get<int>();
	} else if (payload.contains("time")) {
		if (!payload["time"].is_number()) {
			throw CommandError("INVALID_FIELD", "'time' must be a number");
		}
		frame = static_cast<int>(std::lround(payload["time"].get<double>() / scene_->intervalS));
	} else {
		throw CommandError("MISSING_FIELD", "'seek' requires 'frame' or 'time'");
	}
	setFrame(frame);
	return nlohmann::json{{"frame", currentFrame_}, {"time_s", scene_->frameTime(currentFrame_)}};
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
	result["time_s"] = scene_ ? scene_->frameTime(currentFrame_) : 0.0;
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
	response["time_s"] = scene_->frameTime(currentFrame_);
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

} // namespace secgui
