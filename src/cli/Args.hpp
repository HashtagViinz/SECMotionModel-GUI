#pragma once

#include <string>
#include <vector>

namespace secgui {

struct Args {
	std::string scenePath;
	std::string importPath;        // scene/run directory loaded at startup
	std::string tasksPath;
	std::string scriptPath;        // "-" reads from stdin
	std::string screenshotPath;
	std::string outputDir = ".";
	std::string backend = "auto";  // auto|vulkan|d3d12|metal|opengl|null
	std::string cameraMode;
	std::string cameraTarget;
	std::string recordScriptPath;
	std::string logLevel = "info";
	int width = 1280;
	int height = 720;
	int frame = -1;
	int windowFrames = 0;          // auto-close the window after N frames (0 = unlimited)
	double time = -1.0;
	double speed = 1.0;
	bool headless = false;
	bool strict = false;
	bool validateOnly = false;
	bool playOnStart = false;
	bool showHelp = false;
	bool showVersion = false;
	bool quiet = false;
	std::vector<std::string> layers; // raw name=on|off entries

	bool wantsScript() const { return !scriptPath.empty(); }
	bool wantsScreenshot() const { return !screenshotPath.empty(); }
};

// Throws std::runtime_error on invalid arguments.
Args parseArgs(int argc, char** argv);

std::string helpText();
std::string versionText();

} // namespace secgui
