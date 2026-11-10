#include "scene/SceneLoader.hpp"

#include "util/Log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace secgui {

namespace {

std::filesystem::path resolveManifest(const std::filesystem::path& path) {
	if (std::filesystem::is_directory(path)) {
		return path / "manifest.json";
	}
	return path;
}

double numberField(const nlohmann::json& object, const char* key, double fallback) {
	if (!object.contains(key)) {
		return fallback;
	}
	const auto& value = object.at(key);
	if (!value.is_number()) {
		throw std::runtime_error(std::string("scene field '") + key + "' must be a number");
	}
	return value.get<double>();
}

std::string stringField(const nlohmann::json& object, const char* key, const std::string& fallback) {
	if (!object.contains(key)) {
		return fallback;
	}
	const auto& value = object.at(key);
	if (!value.is_string()) {
		throw std::runtime_error(std::string("scene field '") + key + "' must be a string");
	}
	return value.get<std::string>();
}

} // namespace

SceneMetadata SceneLoader::load(const std::filesystem::path& path) {
	const std::filesystem::path manifestPath = resolveManifest(path);
	if (!std::filesystem::exists(manifestPath)) {
		throw std::runtime_error("scene manifest not found: " + manifestPath.string());
	}

	std::ifstream input(manifestPath);
	if (!input) {
		throw std::runtime_error("cannot open scene manifest: " + manifestPath.string());
	}

	nlohmann::json doc;
	try {
		input >> doc;
	} catch (const nlohmann::json::exception& error) {
		throw std::runtime_error("invalid scene JSON in " + manifestPath.string() + ": " + error.what());
	}
	if (!doc.is_object()) {
		throw std::runtime_error("scene manifest must be a JSON object: " + manifestPath.string());
	}

	SceneMetadata meta;
	meta.manifestPath = manifestPath;
	meta.rootDir = manifestPath.parent_path();
	meta.version = doc.value("version", 1);
	if (meta.version != 1) {
		throw std::runtime_error("unsupported scene version " + std::to_string(meta.version) + " (expected 1)");
	}

	meta.t0 = stringField(doc, "t0", "");
	meta.intervalS = numberField(doc, "interval_s", 2.0);
	if (meta.intervalS <= 0.0) {
		throw std::runtime_error("scene field 'interval_s' must be positive");
	}
	meta.durationS = numberField(doc, "duration_s", 0.0);

	if (doc.contains("terminal")) {
		const auto& terminal = doc.at("terminal");
		if (!terminal.is_object()) {
			throw std::runtime_error("scene field 'terminal' must be an object");
		}
		meta.terminalName = stringField(terminal, "name", "TERMINAL");
		meta.terminalLat = numberField(terminal, "lat", 0.0);
		meta.terminalLon = numberField(terminal, "lon", 0.0);
	}
	if (doc.contains("observer")) {
		const auto& observer = doc.at("observer");
		if (!observer.is_object()) {
			throw std::runtime_error("scene field 'observer' must be an object");
		}
		meta.observerName = stringField(observer, "name", "OBSERVER");
		meta.observerLat = numberField(observer, "lat", 0.0);
		meta.observerLon = numberField(observer, "lon", 0.0);
	}

	meta.earthRadiusKm = numberField(doc, "earth_radius_km", 6371.0);
	meta.phiMaxDeg = numberField(doc, "phi_max_deg", 40.0);
	meta.phiBufferDeg = numberField(doc, "phi_buffer_deg", 15.0);

	if (doc.contains("frames")) {
		const auto& frames = doc.at("frames");
		if (!frames.is_object()) {
			throw std::runtime_error("scene field 'frames' must be an object");
		}
		if (!frames.contains("count") || !frames.at("count").is_number_integer()) {
			throw std::runtime_error("scene field 'frames.count' must be an integer");
		}
		meta.frameCount = frames.at("count").get<int>();
		meta.framesPattern = stringField(frames, "pattern", "frames/%06d.json");
	} else if (doc.contains("frame_count")) {
		meta.frameCount = doc.at("frame_count").get<int>();
		meta.framesPattern = stringField(doc, "frames_pattern", "frames/%06d.json");
	}

	if (meta.frameCount <= 0) {
		throw std::runtime_error("scene must declare a positive frame count");
	}
	if (meta.durationS <= 0.0) {
		meta.durationS = static_cast<double>(meta.frameCount) * meta.intervalS;
	}

	meta.tasksPath = stringField(doc, "tasks", "");

	logDebug("loaded scene manifest: " + manifestPath.string()
	         + " frames=" + std::to_string(meta.frameCount)
	         + " interval=" + std::to_string(meta.intervalS));
	return meta;
}

namespace {

Vec3 parseVector3(const nlohmann::json& value, const std::string& context) {
	if (!value.is_array() || value.size() != 3) {
		throw std::runtime_error("frame field '" + context + "' must be an array of 3 numbers");
	}
	Vec3 vector;
	const double components[3] = {
		value.at(0).get<double>(),
		value.at(1).get<double>(),
		value.at(2).get<double>(),
	};
	for (const double component : components) {
		if (!std::isfinite(component)) {
			throw std::runtime_error("frame field '" + context + "' contains a non-finite number");
		}
	}
	vector.x = components[0];
	vector.y = components[1];
	vector.z = components[2];
	if (vector.norm() < 1.0) {
		throw std::runtime_error("frame field '" + context + "' has an implausible radius");
	}
	return vector;
}

std::optional<double> optionalNumber(const nlohmann::json& object, const char* key) {
	if (!object.contains(key) || object.at(key).is_null()) {
		return std::nullopt;
	}
	const double value = object.at(key).get<double>();
	if (!std::isfinite(value)) {
		return std::nullopt;
	}
	return value;
}

std::string stringOr(const nlohmann::json& object, const char* key, const std::string& fallback = {}) {
	if (!object.contains(key)) {
		return fallback;
	}
	const auto& value = object.at(key);
	if (!value.is_string()) {
		return fallback;
	}
	return value.get<std::string>();
}

EntityFrame parseEntity(const nlohmann::json& document, const std::string& key, const std::string& fallback) {
	EntityFrame entity;
	entity.name = fallback;
	if (!document.contains(key)) {
		return entity;
	}
	const auto& object = document.at(key);
	if (!object.is_object()) {
		throw std::runtime_error("frame field '" + key + "' must be an object");
	}
	entity.name = stringOr(object, "name", fallback);
	entity.lat = object.value("lat", 0.0);
	entity.lon = object.value("lon", 0.0);
	if (object.contains("ecef_km")) {
		entity.ecefKm = parseVector3(object.at("ecef_km"), key + ".ecef_km");
	}
	return entity;
}

} // namespace

std::filesystem::path SceneLoader::framePath(const SceneMetadata& scene, int frameIndex) {
	const std::string pattern = scene.framesPattern.empty() ? "frames/%06d.json" : scene.framesPattern;
	char buffer[128];
	std::snprintf(buffer, sizeof(buffer), pattern.c_str(), frameIndex);
	return scene.rootDir / buffer;
}

FrameData SceneLoader::loadFrame(const SceneMetadata& scene, int frameIndex) {
	if (frameIndex < 0 || frameIndex >= scene.frameCount) {
		throw std::runtime_error("frame " + std::to_string(frameIndex) + " is outside [0, "
		                         + std::to_string(scene.frameCount - 1) + "]");
	}
	const std::filesystem::path path = framePath(scene, frameIndex);
	if (!std::filesystem::exists(path)) {
		throw std::runtime_error("frame file not found: " + path.string());
	}

	std::ifstream input(path);
	nlohmann::json document;
	try {
		input >> document;
	} catch (const nlohmann::json::exception& error) {
		throw std::runtime_error("invalid frame JSON in " + path.string() + ": " + error.what());
	}
	if (!document.is_object()) {
		throw std::runtime_error("frame must be a JSON object: " + path.string());
	}

	FrameData frame;
	frame.index = document.value("index", frameIndex);
	frame.t = document.value("t", scene.frameTime(frameIndex));
	frame.time = stringOr(document, "time", "");
	frame.vessel = parseEntity(document, "vessel", scene.terminalName);
	frame.observer = parseEntity(document, "observer", scene.observerName);

	if (!document.contains("satellites") || !document.at("satellites").is_array()) {
		throw std::runtime_error("frame field 'satellites' must be an array: " + path.string());
	}

	std::set<std::string> seen;
	for (const auto& item : document.at("satellites")) {
		if (!item.is_object()) {
			throw std::runtime_error("frame satellite entries must be objects: " + path.string());
		}
		SatelliteFrame satellite;
		satellite.id = stringOr(item, "id", "");
		if (satellite.id.empty()) {
			throw std::runtime_error("frame satellite is missing an 'id': " + path.string());
		}
		if (!seen.insert(satellite.id).second) {
			throw std::runtime_error("duplicate satellite id '" + satellite.id + "' in " + path.string());
		}
		satellite.ecefKm = parseVector3(item.at("ecef_km"), "satellites[" + satellite.id + "].ecef_km");
		satellite.isAp = item.value("is_ap", false);
		satellite.terminalRef = stringOr(item, "terminal_ref", "");
		satellite.elevDeg = optionalNumber(item, "elev_deg");
		satellite.rangeKm = optionalNumber(item, "range_km");
		satellite.lifeRemainingS = optionalNumber(item, "life_remaining_s");
		if (item.contains("neighbors") && item.at("neighbors").is_array()) {
			for (const auto& neighbor : item.at("neighbors")) {
				if (!neighbor.is_object()) {
					continue;
				}
				NeighborRef reference;
				reference.id = stringOr(neighbor, "id", "");
				if (reference.id.empty()) {
					continue;
				}
				reference.latencyMs = neighbor.value("latency_ms", 0.0);
				satellite.neighbors.push_back(std::move(reference));
			}
		}
		frame.satellites.push_back(std::move(satellite));
	}

	if (frame.satellites.empty()) {
		throw std::runtime_error("frame has no satellites: " + path.string());
	}

	logDebug("loaded frame " + std::to_string(frame.index) + " ("
	         + std::to_string(frame.satellites.size()) + " satellites)");
	return frame;
}

std::vector<TaskEvent> SceneLoader::loadTasks(const SceneMetadata& scene, const std::string& overridePath) {
	std::string tasksPath = !overridePath.empty() ? overridePath : scene.tasksPath;
	if (tasksPath.empty()) {
		return {};
	}
	std::filesystem::path path = tasksPath;
	if (!path.is_absolute()) {
		// A manifest path is relative to the scene; a CLI/override path is
		// relative to the current working directory.
		path = overridePath.empty() ? (scene.rootDir / path) : path;
	}
	if (!std::filesystem::exists(path)) {
		logWarn("task trace not found: " + path.string());
		return {};
	}

	static const std::set<std::string> allowed = {
		"spawn", "hop_start", "hop_end", "compute", "deliver", "complete", "fail",
	};

	std::vector<TaskEvent> events;
	std::ifstream input(path);
	std::string line;
	int lineNumber = 0;
	while (std::getline(input, line)) {
		++lineNumber;
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}
		nlohmann::json document;
		try {
			document = nlohmann::json::parse(line);
		} catch (const nlohmann::json::exception& error) {
			throw std::runtime_error("invalid task JSON in " + path.string() + ":"
			                         + std::to_string(lineNumber) + ": " + error.what());
		}
		const std::string event = !stringOr(document, "event", "").empty()
			? stringOr(document, "event", "")
			: stringOr(document, "type", "");
		if (allowed.find(event) == allowed.end()) {
			continue;
		}
		TaskEvent task;
		task.t = document.value("t", 0.0);
		task.event = event;
		task.task = document.value("task", 0);
		task.node = stringOr(document, "node", "");
		task.from = stringOr(document, "from", "");
		task.to = stringOr(document, "to", "");
		task.type = stringOr(document, "type", "");
		task.algo = stringOr(document, "algo", "");
		task.reason = stringOr(document, "reason", "");
		task.sizeMb = document.value("size_mb", 0.0);
		task.durationS = document.value("duration_s", 0.0);
		task.rttS = document.value("rtt_s", 0.0);
		if (task.task > 0) {
			events.push_back(std::move(task));
		}
	}

	std::stable_sort(events.begin(), events.end(), [](const TaskEvent& left, const TaskEvent& right) {
		return left.t < right.t;
	});

	// The trace only carries the task type on the spawn event. Propagate it to
	// the later events so hop pulses keep the color of their task.
	std::map<int, std::string> taskTypes;
	for (const TaskEvent& event : events) {
		if (!event.type.empty()) {
			taskTypes[event.task] = event.type;
		}
	}
	for (TaskEvent& event : events) {
		if (event.type.empty()) {
			const auto found = taskTypes.find(event.task);
			if (found != taskTypes.end()) {
				event.type = found->second;
			}
		}
	}

	logDebug("loaded " + std::to_string(events.size()) + " task events");
	return events;
}

} // namespace secgui
