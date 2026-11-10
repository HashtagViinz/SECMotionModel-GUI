#include "cli/Args.hpp"
#include "util/Sha256.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++failures;
	}
}

void testSha256() {
	check(secgui::sha256Hex(std::vector<uint8_t>{})
	      == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	      "sha256 of empty input");
	const std::string abc = "abc";
	check(secgui::sha256Hex(reinterpret_cast<const uint8_t*>(abc.data()), abc.size())
	      == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	      "sha256 of 'abc'");
}

void testArgs() {
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
	check(args.headless, "--headless parsed");
	check(args.strict, "--strict parsed");
	check(args.frame == 3, "--frame parsed");
	check(args.width == 640, "--width parsed");
	check(args.height == 360, "--height parsed");
	check(args.backend == "vulkan", "--backend parsed");
	check(args.scenePath == "examples/minimal/scene.json", "--scene parsed");
	check(args.screenshotPath == "out/x.png", "--screenshot parsed");

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
	check(threw, "unknown option throws");
}

} // namespace

int main() {
	testSha256();
	testArgs();
	if (failures != 0) {
		std::cerr << failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "all tests passed\n";
	return 0;
}
