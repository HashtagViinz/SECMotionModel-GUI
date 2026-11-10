#pragma once

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace secgui {

class Application;

// Error carrying a stable machine-readable code for the JSONL protocol.
class CommandError : public std::runtime_error {
public:
	CommandError(std::string code, const std::string& message)
		: std::runtime_error(message), code_(std::move(code)) {}

	const std::string& code() const { return code_; }

private:
	std::string code_;
};

class CommandBus {
public:
	explicit CommandBus(Application& application) : application_(application) {}

	// Throws CommandError on failure.
	nlohmann::json dispatch(const std::string& command, const nlohmann::json& payload);

	bool quitRequested() const;

private:
	Application& application_;
};

} // namespace secgui
