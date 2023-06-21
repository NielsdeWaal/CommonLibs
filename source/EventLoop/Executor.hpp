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
	}

	template<typename T>
	void Startup(T configure)
	{
		for(std::size_t i = 0; i < m_threads.size(); ++i)
		{
			m_threads[i] = std::thread([threadId = i, configure, this] {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				spdlog::info("From id: {}, pinned to core: {}", threadId, sched_getcpu());
				EventLoop loop(threadId);
				auto program = configure(loop);
				loop.Run();
			});

			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(m_threadMap[i], &cpuset);
			int code = pthread_setaffinity_np(m_threads[i].native_handle(), sizeof(cpu_set_t), &cpuset);
			if(code != 0)
			{
				spdlog::error("Failed to call pthread_setaffinity_np, rc: {}", code);
			}
		}
	}

	void Wait()
	{
		for(auto& thread: m_threads)
		{
			thread.join();
		}
	}

private:
	std::vector<CpuLocation> m_topology{GetMachineTopology()};
	std::vector<std::thread> m_threads;
	std::vector<int> m_threadMap;
};

} // namespace EventLoop

#endif
