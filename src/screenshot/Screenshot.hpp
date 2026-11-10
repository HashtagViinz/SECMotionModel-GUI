#pragma once

#include "gpu/GpuContext.hpp"
#include "render/Renderer.hpp"

#include <filesystem>
#include <string>

namespace secgui {

struct ScreenshotStats {
	double mean = 0.0;
	double stddev = 0.0;
	bool nonblank = false;
};

struct ScreenshotResult {
	std::filesystem::path path;
	unsigned width = 0;
	unsigned height = 0;
	std::size_t bytes = 0;
	std::string sha256;
	ScreenshotStats stats;
};

class ScreenshotService {
public:
	// Renders one frame offscreen at the requested size and writes it as PNG.
	static ScreenshotResult capture(GpuContext& gpu, Renderer& renderer,
	                                unsigned width, unsigned height,
	                                const std::filesystem::path& path);
};

} // namespace secgui
