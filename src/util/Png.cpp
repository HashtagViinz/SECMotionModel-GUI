#include "util/Png.hpp"
#include "util/Log.hpp"

#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace secgui {

bool writePng(const std::filesystem::path& path, int width, int height, int channels,
              const uint8_t* pixels) {
	if (width <= 0 || height <= 0 || (channels != 1 && channels != 2 && channels != 3 && channels != 4)) {
		logError("writePng: invalid arguments");
		return false;
	}
	// stb_image_write uses a narrow-char path; on Windows this is UTF-8 in
	// practice for ASCII paths. Non-ASCII output paths are not supported yet.
	const std::string narrow = path.string();
	const int result = stbi_write_png(narrow.c_str(), width, height, channels, pixels, width * channels);
	if (result == 0) {
		logError("writePng: failed to write '" + narrow + "'");
		return false;
	}
	return true;
}

bool writePng(const std::filesystem::path& path, int width, int height, int channels,
              const std::vector<uint8_t>& pixels) {
	return writePng(path, width, height, channels, pixels.data());
}

} // namespace secgui
