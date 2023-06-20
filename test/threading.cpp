#include <catch2/catch.hpp>

#include "EventLoop.h"
#include "Executor.hpp"

TEST_CASE("Threading test", "[Eventloop Threading]")
{
	struct handler
	{
		handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{}

		~handler()
		{
			spdlog::info("destructing handler");
		}

		void Configure()
		{
			spdlog::info("from handler");
		}

	private:
		EventLoop::EventLoop& mEv;
	};

	EventLoop::Executor exec({4, 6});
	exec.Startup([](EventLoop::EventLoop& loop) {
		handler hand(loop);
		hand.Configure();
		return hand;
	});
	exec.Wait();
}
