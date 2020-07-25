#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch.hpp>

#include "EventLoop.h"

#include <csignal>

using namespace std::chrono_literals;

TEST_CASE("EventLoop Timer callback", "[EventLoop Timer]") {
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	bool timerConfirmation = false;

	EventLoop::EventLoop::Timer evTimer = EventLoop::EventLoop::Timer(EventLoop::EventLoop::Timer(1s, EventLoop::EventLoop::TimerType::Oneshot,
				[&](){
					timerConfirmation = true;
					std::raise(SIGINT);
					}
				));
	loop.AddTimer(&evTimer);

	loop.Run();

	REQUIRE(timerConfirmation == true);
}

TEST_CASE("EventLoop Timer timing", "[EventLoop Timer]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();
	using TimePoint = std::chrono::high_resolution_clock::time_point;

	auto timeStart = std::chrono::high_resolution_clock::now();
	auto timeEnd = std::chrono::high_resolution_clock::now();
	EventLoop::EventLoop::Timer evTimer = EventLoop::EventLoop::Timer(EventLoop::EventLoop::Timer(1s, EventLoop::EventLoop::TimerType::Oneshot,
				[&](){
					timeEnd = std::chrono::high_resolution_clock::now();
					std::raise(SIGINT);
					}
				));
	loop.AddTimer(&evTimer);

	timeStart = std::chrono::high_resolution_clock::now();
	loop.Run();

	REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(timeEnd - timeStart).count() == Approx(1000).epsilon(0.01));
}

TEST_CASE("EventLoop Register logger", "[EventLoop Logging]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	std::shared_ptr<spdlog::logger> logger;

	REQUIRE(logger == nullptr);

	logger = loop.RegisterLogger("TestingLogger");

	REQUIRE(logger != nullptr);
	REQUIRE(logger->name() == "TestingLogger");
}
