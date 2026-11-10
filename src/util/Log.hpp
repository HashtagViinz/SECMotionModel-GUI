#pragma once

#include <string>

namespace secgui {

enum class LogLevel {
	Debug = 0,
	Info,
	Warn,
	Error,
};

void setLogLevel(LogLevel level);
LogLevel logLevel();
void log(LogLevel level, const std::string& message);
void logDebug(const std::string& message);
void logInfo(const std::string& message);
void logWarn(const std::string& message);
void logError(const std::string& message);

} // namespace secgui
