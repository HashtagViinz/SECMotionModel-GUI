#pragma once

#include "cli/Args.hpp"
#include "gpu/GpuContext.hpp"
#include "render/Camera.hpp"
#include "render/Renderer.hpp"
#include "scene/SceneLoader.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace secgui {

class Application {
public:
	explicit Application(Args args);

	int run();

	const Args& args() const { return args_; }
	GpuContext& gpu() { return gpu_; }
	bool quitRequested() const { return quit_; }

	nlohmann::json commandOpen(const nlohmann::json& payload);
	nlohmann::json commandSeek(const nlohmann::json& payload);
	nlohmann::json commandStep(const nlohmann::json& payload);
	nlohmann::json commandPlay(const nlohmann::json& payload);
	nlohmann::json commandPause(const nlohmann::json& payload);
	nlohmann::json commandSpeed(const nlohmann::json& payload);
	nlohmann::json commandCamera(const nlohmann::json& payload);
	nlohmann::json commandLayer(const nlohmann::json& payload);
	nlohmann::json commandSelect(const nlohmann::json& payload);
	nlohmann::json commandReset(const nlohmann::json& payload);
	nlohmann::json commandState(const nlohmann::json& payload);
	nlohmann::json commandScreenshot(const nlohmann::json& payload);
	nlohmann::json commandValidate(const nlohmann::json& payload);
	nlohmann::json commandQuit(const nlohmann::json& payload);

private:
	void loadScene(const std::filesystem::path& path);
	void applyInitialState();
	void requireScene() const;
	void reloadFrame();
	void buildEarthGrid();
	RenderFrame buildRenderFrame(unsigned width, unsigned height) const;
	nlohmann::json sceneInfo() const;
	void setFrame(int frame);
	void printReadyLine() const;

	Args args_;
	GpuContext gpu_;
	Renderer renderer_;
	std::optional<SceneMetadata> scene_;
	std::optional<FrameData> frameData_;
	std::vector<TaskEvent> tasks_;
	CameraState camera_;
	double earthRadiusKm_ = 6371.0;
	double phiMaxDeg_ = 40.0;
	double phiBufferDeg_ = 15.0;
	int currentFrame_ = 0;
	bool playing_ = false;
	bool quit_ = false;
	double speed_ = 1.0;
	std::string cameraMode_ = "orbit";
	std::string cameraTarget_;
	std::map<std::string, bool> layers_;
	std::string selected_;
	std::string tasksOverride_;
};

} // namespace secgui
