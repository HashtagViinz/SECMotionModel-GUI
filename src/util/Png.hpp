#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace secgui {

// Writes an 8-bit PNG. `channels` is 1, 2, 3 or 4.
bool writePng(const std::filesystem::path& path, int width, int height, int channels,
              const uint8_t* pixels);

bool writePng(const std::filesystem::path& path, int width, int height, int channels,
              const std::vector<uint8_t>& pixels);

} // namespace secgui
