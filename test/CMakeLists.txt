set(testSources main.cpp)

find_package(Catch2 REQUIRED)
add_executable(tests ${testSources})
target_link_libraries(tests PUBLIC
	EventLoop

	Common
	spdlog
	cpptoml
	Catch2::Catch2
	)

include(CTest)
include(Catch)
catch_discover_tests(tests)
