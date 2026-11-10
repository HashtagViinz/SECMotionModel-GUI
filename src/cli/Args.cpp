#include "cli/Args.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace secgui {

namespace {

std::string requireValue(const std::string& name, int& index, int argc, char** argv) {
	if (index + 1 >= argc) {
		throw std::runtime_error("missing value for option " + name);
	}
	return argv[++index];
}

int parseInt(const std::string& name, const std::string& value, int minimum) {
	try {
		const int parsed = std::stoi(value);
		if (parsed < minimum) {
			throw std::runtime_error("option " + name + " must be >= " + std::to_string(minimum));
		}
		return parsed;
	} catch (const std::invalid_argument&) {
		throw std::runtime_error("invalid integer for " + name + ": '" + value + "'");
	} catch (const std::out_of_range&) {
		throw std::runtime_error("integer out of range for " + name + ": '" + value + "'");
	}
}

double parseDouble(const std::string& name, const std::string& value) {
	try {
		return std::stod(value);
	} catch (const std::invalid_argument&) {
		throw std::runtime_error("invalid number for " + name + ": '" + value + "'");
	} catch (const std::out_of_range&) {
		throw std::runtime_error("number out of range for " + name + ": '" + value + "'");
	}
}

} // namespace

Args parseArgs(int argc, char** argv) {
	Args args;
	for (int i = 1; i < argc; ++i) {
		std::string token = argv[i];
		std::string value;
		bool hasInlineValue = false;
		if (token.rfind("--", 0) == 0) {
			const auto equal = token.find('=');
			if (equal != std::string::npos) {
				value = token.substr(equal + 1);
				token = token.substr(0, equal);
				hasInlineValue = true;
			}
		}

		auto nextValue = [&](const std::string& name) -> std::string {
			if (hasInlineValue) {
				return value;
			}
			return requireValue(name, i, argc, argv);
		};

		if (token == "-h" || token == "--help") {
			args.showHelp = true;
		} else if (token == "-v" || token == "--version") {
			args.showVersion = true;
		} else if (token == "-q" || token == "--quiet") {
			args.quiet = true;
		} else if (token == "--scene") {
			args.scenePath = nextValue(token);
		} else if (token == "--tasks") {
			args.tasksPath = nextValue(token);
		} else if (token == "--script") {
			args.scriptPath = nextValue(token);
		} else if (token == "--screenshot") {
			args.screenshotPath = nextValue(token);
		} else if (token == "--output-dir") {
			args.outputDir = nextValue(token);
		} else if (token == "--backend") {
			args.backend = nextValue(token);
		} else if (token == "--camera") {
			args.cameraMode = nextValue(token);
		} else if (token == "--target") {
			args.cameraTarget = nextValue(token);
		} else if (token == "--record-script") {
			args.recordScriptPath = nextValue(token);
		} else if (token == "--width") {
			args.width = parseInt(token, nextValue(token), 1);
		} else if (token == "--height") {
			args.height = parseInt(token, nextValue(token), 1);
		} else if (token == "--frame") {
			args.frame = parseInt(token, nextValue(token), 0);
		} else if (token == "--time") {
			args.time = parseDouble(token, nextValue(token));
		} else if (token == "--speed") {
			args.speed = parseDouble(token, nextValue(token));
		} else if (token == "--layer") {
			args.layers.push_back(nextValue(token));
		} else if (token == "--log-level") {
			args.logLevel = nextValue(token);
		} else if (token == "--headless") {
			args.headless = true;
		} else if (token == "--strict") {
			args.strict = true;
		} else if (token == "--validate-only") {
			args.validateOnly = true;
		} else {
			throw std::runtime_error("unknown option: " + token);
		}
	}
	return args;
}

std::string versionText() {
	return std::string("secmotion-gui ") + SECGUI_VERSION;
}

std::string helpText() {
	std::ostringstream out;
	out << versionText() << "\n\n"
	    << "Usage: secmotion-gui [options]\n\n"
	    << "Scene and script:\n"
	    << "  --scene PATH           scene manifest (scene.json or a scene directory)\n"
	    << "  --tasks PATH           tasks JSONL (overrides manifest)\n"
	    << "  --script PATH          JSONL command script ('-' reads stdin)\n"
	    << "  --output-dir DIR       directory for generated files (default: .)\n"
	    << "  --record-script PATH   record GUI actions as JSONL commands\n\n"
	    << "Rendering:\n"
	    << "  --headless             no window, offscreen rendering only\n"
	    << "  --screenshot PATH      render one frame and exit\n"
	    << "  --frame N              frame index for --screenshot\n"
	    << "  --time S               time in seconds for --screenshot\n"
	    << "  --width N --height N   screenshot size (default: 1280x720)\n"
	    << "  --backend NAME         auto|vulkan|d3d12|metal|opengl|null\n"
	    << "  --camera MODE          orbit|topdown|vessel|satellite|free\n"
	    << "  --target NAME          camera target entity\n"
	    << "  --layer NAME=on|off    initial layer state\n\n"
	    << "Diagnostics:\n"
	    << "  --validate-only        validate the scene and exit (no GPU)\n"
	    << "  --strict               fail on the first invalid command\n"
	    << "  --log-level LEVEL      debug|info|warn|error\n"
	    << "  -q, --quiet            only JSON on stdout\n"
	    << "  -v, --version          print version\n"
	    << "  -h, --help             print this help\n";
	return out.str();
}

} // namespace secgui
