#include <catch2/catch.hpp>
#include <thread>

#include "EventLoop.h"
#include "Executor.hpp"
#include "TSC.h"

TEST_CASE("Threading test", "[Eventloop Threading]")
{
	struct handler
	{
		explicit handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			// mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
			using namespace Common::literals;
			evTimer = EventLoop::EventLoop::Timer(Common::MONOTONIC_CLOCK::ToCycles(mEv.GetThreadId()), EventLoop::EventLoop::TimerType::Oneshot, [&]() {
				spdlog::info("thread {} timer finished", mEv.GetThreadId());
				REQUIRE(mConfigured);
				mEv.Stop();
			});
			mEv.AddTimer(&evTimer);
		}

		void Configure()
		{
			spdlog::info("Configured handler on thread {}", mEv.GetThreadId());
			mConfigured = true;
		}

	private:
		EventLoop::EventLoop& mEv;
		EventLoop::EventLoop::Timer evTimer;
		bool mConfigured{false};
	};

	EventLoop::Executor exec({4, 6});
	exec.Startup([](EventLoop::EventLoop& loop) {
		handler hand(loop);
		hand.Configure();
		return hand;
	});
	exec.Wait();
}

TEST_CASE("Message passing", "[Eventloop Threading]") {
	
}
