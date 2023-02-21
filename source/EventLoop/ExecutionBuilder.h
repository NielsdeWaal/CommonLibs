#ifndef EXECUTIONBUILDER_H_
#define EXECUTIONBUILDER_H_

#include <cstddef>
#include <pthread.h>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

class ExecutionBuilder
{
public:
	ExecutionBuilder() = default;
	ExecutionBuilder(std::vector<std::size_t> cpuMask)
	{
		pthread_attr_t pa;
		auto r = pthread_attr_init(&pa);
		if(r == 0)
		{
			spdlog::critical("Failed to initialize pthread attributes");
			throw std::runtime_error("pthread_attr_init");
		}

		r = pthread_create(pthread_t* __restrict __newthread,
			const pthread_attr_t* __restrict __attr,
			void* (*__start_routine)(void*),
			void* __restrict __arg)
	}

private:
	std::vector<pthread_t> mThreads;
};

#endif // EXECUTIONBUILDER_H_
