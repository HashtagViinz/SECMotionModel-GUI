#include "cli/JsonlDriver.hpp"

#include "util/Log.hpp"

#include <istream>
#include <ostream>

namespace secgui {

namespace {

nlohmann::json errorResult(int sequence, const std::string& command,
                           const std::string& code, const std::string& message) {
	nlohmann::json result;
	result["protocol"] = 1;
	result["seq"] = sequence;
	result["cmd"] = command;
	result["ok"] = false;
	result["error"] = nlohmann::json{{"code", code}, {"message", message}};
	return result;
}

} // namespace

int JsonlDriver::run(std::istream& input, std::ostream& output) {
	std::string line;
	int sequence = 0;

	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}
		++sequence;

		nlohmann::json document;
		try {
			document = nlohmann::json::parse(line);
		} catch (const nlohmann::json::exception& error) {
			output << errorResult(sequence, "", "PARSE_ERROR", error.what()).dump() << "\n";
			output.flush();
			if (strict_) {
				return 5;
			}
			continue;
		}

		if (!document.is_object() || !document.contains("cmd") || !document["cmd"].is_string()) {
			output << errorResult(sequence, "", "MISSING_CMD", "command object must contain a string 'cmd' field").dump() << "\n";
			output.flush();
			if (strict_) {
				return 5;
			}
			continue;
		}

		const std::string command = document["cmd"].get<std::string>();
		nlohmann::json payload = document;
		payload.erase("cmd");
		payload.erase("protocol");
		payload.erase("seq");

		nlohmann::json result;
		try {
			result = bus_.dispatch(command, payload);
		} catch (const CommandError& error) {
			output << errorResult(sequence, command, error.code(), error.what()).dump() << "\n";
			output.flush();
			if (strict_) {
				return 5;
			}
			continue;
		} catch (const std::exception& error) {
			output << errorResult(sequence, command, "INTERNAL_ERROR", error.what()).dump() << "\n";
			output.flush();
			if (strict_) {
				return 5;
			}
			continue;
		}

		nlohmann::json response;
		response["protocol"] = 1;
		response["seq"] = sequence;
		response["cmd"] = command;
		response["ok"] = true;
		response["result"] = std::move(result);
		output << response.dump() << "\n";
		output.flush();

		if (bus_.quitRequested()) {
			break;
		}
	}

	return 0;
}

} // namespace secgui
