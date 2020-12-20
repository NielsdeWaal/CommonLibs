#ifndef TSC_H
#define TSC_H

#include <algorithm>
#include <array>
#include <numeric>

#include <cstdint>
#include <ctime>
#include <x86intrin.h>

namespace Common {

using MONOTONIC_TIME = std::uint64_t;

static inline uint64_t rdtsc()
{
	// return __rdtsc();
	unsigned int lo;
	unsigned int hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

namespace Clock {
static inline uint64_t nanos()
{
	timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
} // namespace Clock

constexpr size_t SAMPLES = 101;
constexpr uint64_t DELAY_NANOS = 10000; // nanos 1us

std::uint64_t DoSample();

std::uint64_t TscFromCal();

namespace MONOTONIC_CLOCK {

[[nodiscard]] MONOTONIC_TIME Now();

static std::uint64_t TSCFreq()
{
	static uint64_t freq = TscFromCal();
	return freq;
}

[[maybe_unused]] static std::uint64_t ToNanos(MONOTONIC_TIME diff)
{
	static double tscToNanos = 1000000000.0 / TSCFreq();
	return diff * tscToNanos;
}

[[maybe_unused, nodiscard]] static MONOTONIC_TIME ToCycles(std::uint64_t seconds)
{
	return seconds * TSCFreq();
}

} // namespace MONOTONIC_CLOCK

namespace literals {

constexpr std::uint64_t MILLISECONDS_RATIO = 1000000;
constexpr std::uint64_t SECONDS_RATIO = 1000000000;
Common::MONOTONIC_TIME operator"" _ms(unsigned long long time);
Common::MONOTONIC_TIME operator"" _s(unsigned long long time);

} // namespace literals

} // namespace Common

#endif // TSC_H
