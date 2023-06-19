#include <catch2/catch.hpp>

#include "EventLoop.h"
#include "Executor.hpp"

TEST_CASE("Threading test", "[Eventloop Threading]")
{
	EventLoop::Executor exec({1, 2});
	exec.Startup();
	exec.Wait();
}
