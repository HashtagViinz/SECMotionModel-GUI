add_executable(secgui_tests
	unit_main.cpp
	${CMAKE_SOURCE_DIR}/src/cli/Args.cpp
	${CMAKE_SOURCE_DIR}/src/util/Sha256.cpp
)

target_include_directories(secgui_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(secgui_tests PRIVATE SECGUI_VERSION="test")

if(MSVC)
	target_compile_options(secgui_tests PRIVATE /W4)
else()
	target_compile_options(secgui_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME secgui_tests COMMAND secgui_tests)
