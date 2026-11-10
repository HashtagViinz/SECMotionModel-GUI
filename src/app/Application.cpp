#include "app/Application.hpp"

#include "cli/JsonlDriver.hpp"
#include "commands/CommandBus.hpp"
#include "screenshot/Screenshot.hpp"
#include "util/Log.hpp"

#include <cmath>
#include <fstream>
#include <iostream>

namespace secgui {

namespace {

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

} // namespace

Application::Application(Args args)
	: args_(std::move(args)) {}

void Application::loadScene(const std::filesystem::path& path) {
	scene_ = SceneLoader::load(path);
	currentFrame_ = 0;
	playing_ = false;
	quit_ = false;
}

void Application::requireScene() const {
	if (!scene_) {
		throw CommandError("NO_SCENE", "no scene loaded; run 'open' first");
	}
}

void Application::applyInitialState() {
	if (!args_.cameraMode.empty()) {
		cameraMode_ = args_.cameraMode;
	}
	if (!args_.cameraTarget.empty()) {
		cameraTarget_ = args_.cameraTarget;
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
		renderer_.init(gpu_.device, wgpu::TextureFormat::RGBA8Unorm);
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
	if (payload.contains("mode") && payload["mode"].is_string()) {
		cameraMode_ = payload["mode"].get<std::string>();
	}
	if (payload.contains("target") && payload["target"].is_string()) {
		cameraTarget_ = payload["target"].get<std::string>();
	}
	nlohmann::json result;
	result["mode"] = cameraMode_;
	result["target"] = cameraTarget_;
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
		currentFrame_ = 0;
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
	result["camera"] = {{"mode", cameraMode_}, {"target", cameraTarget_}};
	result["layers"] = layers_;
	result["selected"] = selected_;
	result["backend"] = gpu_.backendName;
	result["adapter"] = gpu_.adapterName;
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
