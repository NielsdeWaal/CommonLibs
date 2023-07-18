#include <iostream>
#include <stdio.h>
#include <string>
#include <vector>
#include <limits>

#include <cpptoml.h>
#include <spdlog/spdlog.h>

#include "EventLoop.h"

#include "Example.h"

using namespace std::chrono_literals;

int main([[maybe_unused]] int argc, [[maybe_unused]] char const* argv[])
{
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%F] [%^%l%$] [%n] %v");
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	// loop.mTimers.push_back(EventLoop::EventLoop::Timer(2s, EventLoop::EventLoop::TimerType::Repeating,
	// [](){spdlog::info("Callback from timer1");})); loop.mTimers.push_back(EventLoop::EventLoop::Timer(1s,
	// EventLoop::EventLoop::TimerType::Repeating, [](){spdlog::info("Callback from timer2");}));

	ExampleApp app(loop);

	app.Configure();
	app.Initialise();

	loop.Run();
	return 0;
}
