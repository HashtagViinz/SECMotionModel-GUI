#include "scene/SceneLoader.hpp"

#include "util/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
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

} // namespace secgui
