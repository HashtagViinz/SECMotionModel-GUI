#include "cli/Args.hpp"
#include "util/Sha256.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int testSha256() {
	assert(secgui::sha256Hex(std::vector<uint8_t>{})
	       == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	const std::string abc = "abc";
	assert(secgui::sha256Hex(reinterpret_cast<const uint8_t*>(abc.data()), abc.size())
	       == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	return 0;
}

int testArgs() {
	std::vector<std::string> storage = {
		"secmotion-gui", "--headless", "--scene", "examples/minimal/scene.json",
		"--screenshot=out/x.png", "--frame", "3", "--width=640", "--height", "360",
		"--strict", "--backend", "vulkan",
	};
	std::vector<char*> argv;
	argv.reserve(storage.size());
	for (std::string& value : storage) {
		argv.push_back(value.data());
	}
	const secgui::Args args = secgui::parseArgs(static_cast<int>(argv.size()), argv.data());
	assert(args.headless);
	assert(args.strict);
	assert(args.frame == 3);
	assert(args.width == 640);
	assert(args.height == 360);
	assert(args.backend == "vulkan");
	assert(args.scenePath == "examples/minimal/scene.json");
	assert(args.screenshotPath == "out/x.png");

	bool threw = false;
	std::vector<std::string> badStorage = {"secmotion-gui", "--unknown"};
	std::vector<char*> badArgv;
	for (std::string& value : badStorage) {
		badArgv.push_back(value.data());
	}
	try {
		(void)secgui::parseArgs(static_cast<int>(badArgv.size()), badArgv.data());
	} catch (const std::exception&) {
		threw = true;
	}
	assert(threw);
	return 0;
}

} // namespace

int main() {
	testSha256();
	testArgs();
	std::cout << "all tests passed\n";
	return 0;
}
