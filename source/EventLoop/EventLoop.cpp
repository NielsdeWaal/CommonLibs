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
		mEpollReturn = epoll_wait(mEpollFd, mEpollEvents, MaxEpollEvents, 0);
		//spdlog::info("epoll_wait returned: {}", mEpollReturn);
		if(mEpollReturn < 0)
		{
			spdlog::critical("Error on epoll");
			throw std::runtime_error("Error on epoll");
		}
		//else if(mEpollReturn == 0)
		//{
		//	spdlog::info("No event on epoll");
		//}
		else
		{
			//spdlog::info("{} events on fd's", mEpollReturn);
			for(int event = 0; event < mEpollReturn; ++event)
			{
				if (mEpollEvents[event].events & EPOLLERR ||
					mEpollEvents[event].events & EPOLLHUP ||
					!(mEpollEvents[event].events & EPOLLIN) ||
					!(mEpollEvents[event].events & EPOLLOUT)) // error
				{
					spdlog::critical("epoll event error, fd:{}, errno:{}", mEpollEvents[event].data.fd, errno);
					close(mEpollEvents[event].data.fd);
				}
				else if(mEpollEvents[event].events & EPOLLIN)
				{
					mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorRead(mEpollEvents[event].data.fd);
				}
				else if(mEpollEvents[event].events & EPOLLOUT)
				{
					mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorWrite(mEpollEvents[event].data.fd);
				}
				//else if(mEpollEvents[event].events & EPOLLHUP)
				//{
				//	mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorWrite(mEpollEvents[event].data.fd);
				//}
				else
				{
					spdlog::warn("Unhandled event:{} on fd:{}", mEpollEvents[event].data.fd, mEpollEvents[event].events);
				}
			}
		}

		for(const auto& [callback, latencyClass] : mCallbacks)
		{
			//TODO Implement latencyclass
			callback->OnEventLoopCallback();
		}

		for(const auto& timer : mTimers)
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

void EventLoop::RegisterFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler)
{
	struct epoll_event event;
	event.data.fd = fd;
	event.events = events;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) == -1)
	{
		spdlog::critical("Failed to add fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to add fd to epoll interface");
	}
	mFdHandlers.insert({fd, handler});
	spdlog::info("Registered Fd: {}", fd);
}

void EventLoop::ModifyFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler)
{
	struct epoll_event event;
	event.data.fd = fd;
	event.events = events;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, fd, &event) == -1)
	{
		spdlog::critical("Failed to mod fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to mod fd to epoll interface");
	}
	mFdHandlers.insert({fd, handler});
	spdlog::info("Modified Fd: {}", fd);
}

void EventLoop::UnregisterFiledescriptor(int fd)
{
	struct epoll_event event;
	event.data.fd = fd;
	event.events = 0;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, &event) == -1)
	{
		spdlog::critical("Failed to del fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to del fd to epoll interface");
	}

	const auto lookup = mFdHandlers.find(fd);
	mFdHandlers.erase(lookup);

	spdlog::info("Unregistered Fd: {}", fd);
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
