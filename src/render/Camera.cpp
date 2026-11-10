#include "render/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace secgui {

namespace {

Vec3 subtract(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 add(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(const Vec3& v, double factor) { return Vec3{v.x * factor, v.y * factor, v.z * factor}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
	return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalized(const Vec3& v) {
	const double length = v.norm();
	if (length <= 0.0) {
		return Vec3{0.0, 0.0, 1.0};
	}
	return scale(v, 1.0 / length);
}

const SatelliteFrame* findSatellite(const FrameData& frame, const std::string& id) {
	for (const SatelliteFrame& satellite : frame.satellites) {
		if (satellite.id == id) {
			return &satellite;
		}
	}
	return nullptr;
}

} // namespace

Mat4 Mat4::identity() {
	Mat4 result;
	for (int i = 0; i < 4; ++i) {
		result.m[static_cast<std::size_t>(i) * 4 + static_cast<std::size_t>(i)] = 1.0f;
	}
	return result;
}

Mat4 Mat4::perspective(float fovYRadians, float aspect, float near, float far) {
	Mat4 result;
	const float f = 1.0f / std::tan(fovYRadians * 0.5f);
	result.m[0] = f / aspect;
	result.m[5] = f;
	result.m[10] = far / (near - far);
	result.m[11] = -1.0f;
	result.m[14] = (near * far) / (near - far);
	return result;
}

Mat4 Mat4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
	const Vec3 zAxis = normalized(subtract(eye, target));
	const Vec3 xAxis = normalized(cross(up, zAxis));
	const Vec3 yAxis = cross(zAxis, xAxis);

	Mat4 result;
	result.m[0] = static_cast<float>(xAxis.x);
	result.m[1] = static_cast<float>(yAxis.x);
	result.m[2] = static_cast<float>(zAxis.x);
	result.m[3] = 0.0f;
	result.m[4] = static_cast<float>(xAxis.y);
	result.m[5] = static_cast<float>(yAxis.y);
	result.m[6] = static_cast<float>(zAxis.y);
	result.m[7] = 0.0f;
	result.m[8] = static_cast<float>(xAxis.z);
	result.m[9] = static_cast<float>(yAxis.z);
	result.m[10] = static_cast<float>(zAxis.z);
	result.m[11] = 0.0f;
	result.m[12] = static_cast<float>(-dot(xAxis, eye));
	result.m[13] = static_cast<float>(-dot(yAxis, eye));
	result.m[14] = static_cast<float>(-dot(zAxis, eye));
	result.m[15] = 1.0f;
	return result;
}

Mat4 Mat4::operator*(const Mat4& other) const {
	Mat4 result;
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			float value = 0.0f;
			for (int k = 0; k < 4; ++k) {
				value += m[static_cast<std::size_t>(k) * 4 + static_cast<std::size_t>(row)]
				       * other.m[static_cast<std::size_t>(column) * 4 + static_cast<std::size_t>(k)];
			}
			result.m[static_cast<std::size_t>(column) * 4 + static_cast<std::size_t>(row)] = value;
		}
	}
	return result;
}

CameraFrame Camera::compute(const CameraState& state, const FrameData& frame,
                            float aspect, double earthRadiusKm) {
	CameraFrame result;
	result.focus = earthCentre();
	double distance = state.distanceKm;
	double pitch = state.pitchDeg;
	const double yaw = state.yawDeg;

	if (state.mode == "vessel") {
		result.focus = frame.vessel.ecefKm;
	} else if (state.mode == "topdown") {
		result.focus = frame.vessel.ecefKm;
		pitch = -90.0;
		distance = state.distanceKm > 0.0 ? state.distanceKm : 2500.0;
	} else if (state.mode == "satellite") {
		const SatelliteFrame* satellite = findSatellite(frame, state.target);
		if (satellite != nullptr) {
			result.focus = satellite->ecefKm;
		} else if (!frame.satellites.empty()) {
			result.focus = frame.satellites.front().ecefKm;
		}
	} else if (state.mode == "free") {
		result.focus = earthCentre();
	}

	const double yawRadians = yaw * 3.14159265358979323846 / 180.0;
	const double pitchRadians = pitch * 3.14159265358979323846 / 180.0;
	const Vec3 direction{
		std::cos(pitchRadians) * std::sin(yawRadians),
		std::cos(pitchRadians) * std::cos(yawRadians),
		std::sin(pitchRadians),
	};

	const double minimumDistance = earthRadiusKm * 1.05;
	const double effectiveDistance = std::max(distance, minimumDistance);
	result.eye = add(result.focus, scale(direction, effectiveDistance));

	const Vec3 up{0.0, 0.0, 1.0};
	result.view = Mat4::lookAt(result.eye, result.focus, up);
	result.right = Vec3{
		static_cast<double>(result.view.m[0]),
		static_cast<double>(result.view.m[4]),
		static_cast<double>(result.view.m[8]),
	};
	result.up = Vec3{
		static_cast<double>(result.view.m[1]),
		static_cast<double>(result.view.m[5]),
		static_cast<double>(result.view.m[9]),
	};
	const float fov = static_cast<float>(state.fieldOfViewDeg * 3.14159265358979323846 / 180.0);
	const float near = static_cast<float>(std::max(1.0, effectiveDistance / 10000.0));
	const float far = static_cast<float>(earthRadiusKm * 100.0);
	result.projection = Mat4::perspective(fov, aspect, near, far);
	return result;
}

} // namespace secgui
