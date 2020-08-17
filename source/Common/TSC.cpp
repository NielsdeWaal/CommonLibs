#include "TSC.h"
#include <cstdint>

namespace Common {

namespace MONOTONIC_CLOCK {
[[nodiscard]] MONOTONIC_TIME Now()
{
	// _mm_lfence();
	MONOTONIC_TIME ret = rdtsc();
	// _mm_lfence();
	return ret;
}

} // namespace MONOTONIC_CLOCK

std::uint64_t DoSample()
{
	_mm_lfence();
	std::uint64_t nsBefore = Clock::nanos();
	std::uint64_t tscBefore = rdtsc();
	while(nsBefore + DELAY_NANOS > Clock::nanos())
		;
	std::uint64_t nsAfter = Clock::nanos();
	std::uint64_t tscAfter = rdtsc();
	return (tscAfter - tscBefore) * 1000000000u / (nsAfter - nsBefore);
}

std::uint64_t TscFromCal()
{
	std::array<std::uint64_t, SAMPLES * 2> samples;

	for(size_t s = 0; s < SAMPLES * 2; s++)
	{
		samples[s] = DoSample();
	}

	// throw out the first half of samples as a warmup
	std::array<uint64_t, SAMPLES> second_half{};
	std::copy(samples.begin() + SAMPLES, samples.end(), second_half.begin());
	std::sort(second_half.begin(), second_half.end());

	// average the middle quintile
	auto third_quintile = second_half.begin() + 2 * SAMPLES / 5;
	std::uint64_t sum = std::accumulate(third_quintile, third_quintile + SAMPLES / 5, (uint64_t)0);

	return sum / (SAMPLES / 5);
}

namespace literals {

Common::MONOTONIC_TIME operator"" _ms(unsigned long long time)
{
	// const double tscToMillies = MILLISECONDS_RATIO / Common::MONOTONIC_CLOCK::TSCFreq();
	// return time * tscToMillies;
	return time * (MONOTONIC_CLOCK::TSCFreq() / 1000);
}

Common::MONOTONIC_TIME operator"" _s(unsigned long long time)
{
	// const double tscToMillies = SECONDS_RATIO / Common::MONOTONIC_CLOCK::TSCFreq();
	// return time * tscToMillies;
	return time * MONOTONIC_CLOCK::TSCFreq();
}

} // namespace literals

} // namespace Common
