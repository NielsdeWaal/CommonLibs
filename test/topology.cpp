#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

#include <catch2/catch.hpp>

#include "EventLoop.h"
#include "Topology.h"

TEST_CASE("SYSFS string parser", "[EventLoop sysfs]")
{
	REQUIRE(EventLoop::ParseList("0") == std::vector<std::size_t>{0});
	REQUIRE(EventLoop::ParseList("0,1") == std::vector<std::size_t>{0, 1});
	REQUIRE(EventLoop::ParseList("0-2") == std::vector<std::size_t>{0, 1, 2});
	REQUIRE(EventLoop::ParseList("4-5") == std::vector<std::size_t>{4, 5});
	REQUIRE(EventLoop::ParseList("1,4-5") == std::vector<std::size_t>{1, 4, 5});
	REQUIRE(EventLoop::ParseList("0,2-3,5") == std::vector<std::size_t>{0, 2, 3, 5});
}

TEST_CASE("SYSFS topology", "[EventLoop sysfs]")
{
	const auto res = EventLoop::GetMachineTopology();
	REQUIRE(res.at(0).numa == 0);
	REQUIRE(res.at(0).package == 0);
	REQUIRE(res.at(0).core == 0);
	REQUIRE(res.at(0).cpu == 0);

	for(const auto& loc: res)
	{
		fmt::print("{}\n", loc);
	}
}
