#ifndef __EXECUTOR_HPP
#define __EXECUTOR_HPP

#include "EventLoop.h"
#include "Topology.h"
#include <deque>
#include <pthread.h>
#include <sched.h>
#include <thread>

namespace EventLoop {

class Executor
{
public:
	Executor() = default;

	explicit Executor(const std::vector<int>& threads)
		: m_threadMap(threads)
	{
		m_threads.resize(threads.size());
		for(std::size_t i = 0; i < m_threads.size(); ++i)
		{
			m_loops.emplace_back();
		}
	}

	template<typename T>
	void Startup(T configure)
	{
		for(std::size_t i = 0; i < m_threads.size(); ++i)
		{
			m_threads[i] = std::thread([id = i, configure, this] {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				spdlog::info("From id: {}, pinned to core: {}", id, sched_getcpu());
				auto program = configure(m_loops[id]);
				std::this_thread::sleep_for(std::chrono::seconds(2));
			});

			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(m_threadMap[i], &cpuset);
			int rc = pthread_setaffinity_np(m_threads[i].native_handle(), sizeof(cpu_set_t), &cpuset);
			if(rc != 0)
			{
				spdlog::error("Failed to call pthread_setaffinity_np, rc: {}", rc);
			}
		}
	}

	void Wait()
	{
		for(auto& t: m_threads)
		{
			t.join();
		}
	}

private:
	std::vector<CpuLocation> m_topology{GetMachineTopology()};
	std::vector<std::thread> m_threads;
	std::vector<int> m_threadMap;
	std::deque<EventLoop> m_loops;
};

} // namespace EventLoop

#endif
