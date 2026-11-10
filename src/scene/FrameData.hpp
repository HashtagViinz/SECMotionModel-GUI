#pragma once

#include <optional>
#include <string>
#include <vector>

namespace secgui {

struct Vec3 {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	double norm() const;
};

struct NeighborRef {
	std::string id;
	double latencyMs = 0.0;
};

struct SatelliteFrame {
	std::string id;
	Vec3 ecefKm;
	bool isAp = false;
	std::string terminalRef;
	std::optional<double> elevDeg;
	std::optional<double> rangeKm;
	std::optional<double> lifeRemainingS;
	std::vector<NeighborRef> neighbors;
};

struct EntityFrame {
	std::string name;
	double lat = 0.0;
	double lon = 0.0;
	Vec3 ecefKm;
};

struct FrameData {
	int index = 0;
	double t = 0.0;
	std::string time;
	EntityFrame vessel;
	EntityFrame observer;
	std::vector<SatelliteFrame> satellites;
};

struct TaskEvent {
	double t = 0.0;
	std::string event;
	int task = 0;
	std::string node;
	std::string from;
	std::string to;
	std::string type;
	std::string algo;
	std::string reason;
	double sizeMb = 0.0;
	double durationS = 0.0;
	double rttS = 0.0;
};

} // namespace secgui
