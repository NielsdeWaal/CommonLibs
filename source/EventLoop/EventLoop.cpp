#include "EventLoop.h"

namespace EventLoop {

EventLoop::EventLoop()
	: mStarted(true)
	, mStatsTimer(1s, TimerType::Repeating, [this](){ PrintStatistics(); })
{
	mEpollFd = epoll_create1(0);
	if(mEpollFd < 0)
	{
		spdlog::critical("Failed to setup epoll interface");
		throw std::runtime_error("Failed to seetup epoll interface");
	}
}

void EventLoop::Run()
{
	spdlog::info("Eventloop has started");
	while (true)
	{
		for(const auto [callback, latencyClass] : mCallbacks)
		{
			callback->OnEventLoopCallback();
		}

		for(auto& timer : mTimers)
		{
			if(timer->CheckTimerExpired())
			{
				timer->mCallback();
				//spdlog::info("Timer has expired after {}", std::chrono::seconds(timer.mDuration).count());
				if(timer->mType == TimerType::Oneshot)
					mTimers.erase(std::remove(std::begin(mTimers), std::end(mTimers), timer), std::end(mTimers));
				else
					timer->UpdateDeadline();
			}
		}

		mCycleCount++;
	}

}

void EventLoop::AddTimer(Timer* timer)
{
	mTimers.push_back(timer);
}

void EventLoop::RegisterCallbackHandler(IEventLoopCallbackHandler* callback, LatencyType latency)
{
	mCallbacks.insert({callback, latency});
}

void EventLoop::RegisterFiledescriptor(int fd, uint32_t events)
{
	struct epoll_event event;
	event.data.fd = fd;
	event.events = events;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) == -1)
	{
		spdlog::critical("Failed to add fd to epoll interface");
		throw std::runtime_error("Failed to add fd to epoll interface");
	}
}

void EventLoop::EnableStatistics()
{
	AddTimer(&mStatsTimer);
	mStatistics = true;
}

void EventLoop::PrintStatistics()
{
	spdlog::info("EventLoop -> Cycles: {}", mCycleCount);
	mCycleCount = 0;
}

}
