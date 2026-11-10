#pragma once

#include <filesystem>
#include <string>

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
	int frameCount = 0;
	std::string framesPattern;
	std::string tasksPath;
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
};

} // namespace secgui
