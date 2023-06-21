#include <catch2/catch.hpp>
#include <thread>

#include "EventLoop.h"
#include "Executor.hpp"

TEST_CASE("Threading test", "[Eventloop Threading]")
{
	struct handler
	{
		handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			// mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
			using namespace Common::literals;
			evTimer = EventLoop::EventLoop::Timer(2_s, EventLoop::EventLoop::TimerType::Oneshot, [&]() {
				spdlog::info("thread {} timer finished", mEv.GetThreadId());
				REQUIRE(true);
				mEv.Stop();
			});
			mEv.AddTimer(&evTimer);
		}

		~handler()
		{
			spdlog::info("destructing handler");
		}

		void Configure()
		{
			spdlog::info("Configured handler on thread {}", mEv.GetThreadId());
		}

	private:
		EventLoop::EventLoop& mEv;
		EventLoop::EventLoop::Timer evTimer;
	};

	EventLoop::Executor exec({4, 6});
	exec.Startup([](EventLoop::EventLoop& loop) {
		handler hand(loop);
		hand.Configure();
		return hand;
	});
	exec.Wait();
}
