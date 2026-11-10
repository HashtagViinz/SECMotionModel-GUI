#include "util/Log.hpp"

#include <cstdio>
#include <mutex>

namespace secgui {

namespace {
LogLevel g_level = LogLevel::Info;
std::mutex g_mutex;

const char* levelLabel(LogLevel level) {
	switch (level) {
	case LogLevel::Debug: return "debug";
	case LogLevel::Info: return "info";
	case LogLevel::Warn: return "warn";
	case LogLevel::Error: return "error";
	}
	return "info";
}
} // namespace

void setLogLevel(LogLevel level) {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_level = level;
}

LogLevel logLevel() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_level;
}

void log(LogLevel level, const std::string& message) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (static_cast<int>(level) < static_cast<int>(g_level)) {
			return;
		}
	}
	std::fprintf(stderr, "[%s] %s\n", levelLabel(level), message.c_str());
}

void logDebug(const std::string& message) { log(LogLevel::Debug, message); }
void logInfo(const std::string& message) { log(LogLevel::Info, message); }
void logWarn(const std::string& message) { log(LogLevel::Warn, message); }
void logError(const std::string& message) { log(LogLevel::Error, message); }

} // namespace secgui
