#include "app/Application.hpp"
#include "cli/Args.hpp"
#include "commands/CommandBus.hpp"
#include "util/Log.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
	secgui::Args args;
	try {
		args = secgui::parseArgs(argc, argv);
	} catch (const std::exception& error) {
		std::cerr << "error: " << error.what() << "\n"
		          << "run 'secmotion-gui --help' for usage\n";
		return 2;
	}

	if (args.showHelp) {
		std::cout << secgui::helpText();
		return 0;
	}
	if (args.showVersion) {
		std::cout << secgui::versionText() << "\n";
		return 0;
	}

	try {
		secgui::Application application(std::move(args));
		return application.run();
	} catch (const secgui::CommandError& error) {
		secgui::logError(std::string("command error [") + error.code() + "]: " + error.what());
		return 5;
	} catch (const std::exception& error) {
		secgui::logError(std::string("fatal error: ") + error.what());
		return 4;
	}
}
