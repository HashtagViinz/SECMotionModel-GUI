#pragma once

#include <webgpu/webgpu.hpp>

#include <string>

namespace secgui {

// WGPUStringView is the C string view used by wgpu-native v24. The
// WebGPU-Cpp wrapper converts to std::string_view only on its own StringView
// type, so callbacks that receive a raw WGPUStringView use this helper.
inline std::string toStdString(WGPUStringView view) {
	if (view.data == nullptr || view.length == 0) {
		return {};
	}
	return std::string(view.data, view.length);
}

} // namespace secgui
