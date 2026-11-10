#pragma once

#include "commands/CommandBus.hpp"

#include <iosfwd>

namespace secgui {

// Reads one JSON command per line and writes one JSON result per line.
class JsonlDriver {
public:
	JsonlDriver(CommandBus& bus, bool strict, bool quiet)
		: bus_(bus), strict_(strict), quiet_(quiet) {}

	int run(std::istream& input, std::ostream& output);

private:
	CommandBus& bus_;
	bool strict_ = false;
	bool quiet_ = false;
};

} // namespace secgui
