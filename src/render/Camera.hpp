#pragma once

#include "scene/FrameData.hpp"

#include <array>
#include <string>

namespace secgui {

// Column-major 4x4 matrix (WGSL layout).
struct Mat4 {
	std::array<float, 16> m{};

	static Mat4 identity();
	static Mat4 perspective(float fovYRadians, float aspect, float near, float far);
	static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
	Mat4 operator*(const Mat4& other) const;
};

struct CameraState {
	std::string mode = "orbit";
	std::string target;
	double distanceKm = 18000.0;
	double yawDeg = 35.0;
	double pitchDeg = 28.0;
	double fieldOfViewDeg = 45.0;
};

struct CameraFrame {
	Mat4 view;
	Mat4 projection;
	Vec3 eye;
	Vec3 focus;
	Vec3 right{1.0, 0.0, 0.0};
	Vec3 up{0.0, 1.0, 0.0};
};

class Camera {
public:
	// Resolves the focus point from the mode/target and returns the matrices.
	static CameraFrame compute(const CameraState& state, const FrameData& frame,
	                           float aspect, double earthRadiusKm = 6371.0);

	// Earth centre helper.
	static Vec3 earthCentre() { return Vec3{0.0, 0.0, 0.0}; }
};

} // namespace secgui
