#include <catch2/catch.hpp>

#include <thread>

#include "TSC.h"

TEST_CASE("TSC Calibration", "[EventLoop Common]")
{
	REQUIRE(Common::TscFromCal() > 0);
	REQUIRE(Common::MONOTONIC_CLOCK::Now() != 0);
	REQUIRE(Common::MONOTONIC_CLOCK::TSCFreq() != 0);
	REQUIRE(Common::MONOTONIC_CLOCK::Now() > 100);
	REQUIRE(Common::MONOTONIC_CLOCK::TSCFreq() > 100);
}

TEST_CASE("TSC timer", "[EventLoop Common]")
{
	using namespace std::chrono_literals;
	Common::MONOTONIC_TIME start = Common::MONOTONIC_CLOCK::Now();
	std::this_thread::sleep_for(100ms);
	Common::MONOTONIC_TIME end = Common::MONOTONIC_CLOCK::Now();
	REQUIRE(Common::MONOTONIC_CLOCK::ToNanos(end - start) == Approx(100000000).epsilon(0.01));
}

TEST_CASE("TSC user literals", "[EVentLoop Common]")
{
	using namespace Common::literals;
	using namespace std::chrono_literals;

	REQUIRE(1_ms > 0);
	REQUIRE(1_s > 0);

	Common::MONOTONIC_TIME now = Common::MONOTONIC_CLOCK::Now();
	Common::MONOTONIC_TIME diff = now + 100_ms;

	CHECK(diff > now);

	Common::MONOTONIC_TIME millisecondDiff = now + 1000_ms;
	Common::MONOTONIC_TIME secondDiff = now + 1_s;

	CHECK(millisecondDiff == Approx(secondDiff).epsilon(0.001));

	CHECK(Common::MONOTONIC_CLOCK::ToCycles(1) == Approx(1_s).epsilon(0.001));

	std::this_thread::sleep_for(100ms);
	Common::MONOTONIC_TIME end = Common::MONOTONIC_CLOCK::Now();
	REQUIRE(diff == Approx(end).epsilon(0.01));
	now = Common::MONOTONIC_CLOCK::Now();
	diff = now + 1_s;
	std::this_thread::sleep_for(1000ms);
	end = Common::MONOTONIC_CLOCK::Now();
	CHECK(diff == Approx(end).epsilon(0.001));
}
