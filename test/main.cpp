#define CATCH_CONFIG_MAIN // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/catch.hpp>

#include "EventLoop.h"
#include "TSC.h"

#include <csignal>

using namespace std::chrono_literals;

TEST_CASE("EventLoop Timer callback", "[EventLoop Timer]")
{
	using namespace Common::literals;
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	bool timerConfirmation = false;

	EventLoop::EventLoop::Timer evTimer =
		EventLoop::EventLoop::Timer(EventLoop::EventLoop::Timer(1_ms, EventLoop::EventLoop::TimerType::Oneshot, [&]() {
			timerConfirmation = true;
			std::raise(SIGINT);
		}));
	loop.AddTimer(&evTimer);

	REQUIRE(evTimer.mEnd > Common::MONOTONIC_CLOCK::Now());
	REQUIRE(evTimer.mEnd == Approx(Common::MONOTONIC_CLOCK::Now() + 1_ms).epsilon(0.001));

	REQUIRE(evTimer.CheckTimerExpired() == false);

	loop.Run();

	REQUIRE(timerConfirmation == true);
}

TEST_CASE("EventLoop Timer timing", "[EventLoop Timer]")
{
	using namespace Common::literals;
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();
	using TimePoint = std::chrono::high_resolution_clock::time_point;

	auto timeStart = std::chrono::high_resolution_clock::now();
	auto timeEnd = std::chrono::high_resolution_clock::now();
	EventLoop::EventLoop::Timer evTimer =
		EventLoop::EventLoop::Timer(100_ms, EventLoop::EventLoop::TimerType::Oneshot, [&]() {
			timeEnd = std::chrono::high_resolution_clock::now();
			std::raise(SIGINT);
		});
	loop.AddTimer(&evTimer);

	timeStart = std::chrono::high_resolution_clock::now();
	loop.Run();

	REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(timeEnd - timeStart).count() ==
			Approx(100).epsilon(0.01));
}

TEST_CASE("EventLoop Timer removal", "[EventLoop Timer]")
{
	using namespace Common::literals;
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	int callCounter = 0;

	EventLoop::EventLoop::Timer evTimer =
		EventLoop::EventLoop::Timer(EventLoop::EventLoop::Timer(1_ms, EventLoop::EventLoop::TimerType::Oneshot, [&]() {
			callCounter++;
			std::raise(SIGINT);
		}));

	EventLoop::EventLoop::Timer AssertTimer =
		EventLoop::EventLoop::Timer(EventLoop::EventLoop::Timer(2_ms, EventLoop::EventLoop::TimerType::Oneshot, [&]() {
			REQUIRE(callCounter == 1);
			std::raise(SIGINT);
		}));
	loop.AddTimer(&evTimer);

	loop.Run();

	loop.RemoveTimer(&evTimer);
	loop.AddTimer(&AssertTimer);

	loop.Run();
	REQUIRE(callCounter == 1);
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

TEST_CASE("EventLoop callback", "[EventLoop callback]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	struct Test : public EventLoop::IEventLoopCallbackHandler
	{
	public:
		Test(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
		}

		void OnEventLoopCallback() final
		{
			REQUIRE(true);
			std::raise(SIGINT);
		}

	private:
		EventLoop::EventLoop& mEv;
	};

	Test callbackTest(loop);

	loop.Run();
}
