#pragma once

#include "scene/FrameData.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace secgui {

struct SceneMetadata {
	int version = 1;
	std::string t0;
	double intervalS = 2.0;
	double durationS = 0.0;
	std::string terminalName;
	double terminalLat = 0.0;
	double terminalLon = 0.0;
	std::string observerName;
	double observerLat = 0.0;
	double observerLon = 0.0;
	double earthRadiusKm = 6371.0;
	double phiMaxDeg = 40.0;
	double phiBufferDeg = 15.0;
	int frameCount = 0;
	std::string framesPattern;
	std::string tasksPath;
	std::string loadsPath;
	std::string trafficPath;
	double cpuCapacity = 5.0;
	double netCapacity = 5.0;
	std::map<std::string, bool> defaultLayers;
	std::filesystem::path rootDir;    // directory containing the manifest
	std::filesystem::path manifestPath;

	double frameTime(int frameIndex) const {
		return static_cast<double>(frameIndex) * intervalS;
	}
};

// Throws std::runtime_error with a human-readable message on failure.
class SceneLoader {
public:
	static SceneMetadata load(const std::filesystem::path& path);

	// Path of a frame file inside the scene directory.
	static std::filesystem::path framePath(const SceneMetadata& scene, int frameIndex);

	// Loads and validates one frame. Throws std::runtime_error on failure.
	static FrameData loadFrame(const SceneMetadata& scene, int frameIndex);

	// Loads the task trace (manifest path unless `overridePath` is set).
	// Returns an empty vector when no trace file is configured.
	static std::vector<TaskEvent> loadTasks(const SceneMetadata& scene,
	                                        const std::string& overridePath = {});

	// Optional v2 telemetry. Returns empty vectors for v1 scenes.
	static std::vector<LoadSample> loadLoads(const SceneMetadata& scene);
	static std::vector<TrafficEvent> loadTraffic(const SceneMetadata& scene);
};

} // namespace secgui
