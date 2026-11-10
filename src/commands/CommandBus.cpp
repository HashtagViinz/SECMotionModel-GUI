#include "commands/CommandBus.hpp"

#include "app/Application.hpp"

namespace secgui {

nlohmann::json CommandBus::dispatch(const std::string& command, const nlohmann::json& payload) {
	if (command == "open") return application_.commandOpen(payload);
	if (command == "seek") return application_.commandSeek(payload);
	if (command == "step") return application_.commandStep(payload);
	if (command == "play") return application_.commandPlay(payload);
	if (command == "pause") return application_.commandPause(payload);
	if (command == "speed") return application_.commandSpeed(payload);
	if (command == "camera") return application_.commandCamera(payload);
	if (command == "layer") return application_.commandLayer(payload);
	if (command == "select") return application_.commandSelect(payload);
	if (command == "reset") return application_.commandReset(payload);
	if (command == "state") return application_.commandState(payload);
	if (command == "screenshot") return application_.commandScreenshot(payload);
	if (command == "validate") return application_.commandValidate(payload);
	if (command == "quit") return application_.commandQuit(payload);

	throw CommandError("UNKNOWN_COMMAND", "unknown command: '" + command + "'");
}

bool CommandBus::quitRequested() const {
	return application_.quitRequested();
}

} // namespace secgui
