#include "EventLoop.h"

namespace EventLoop {

EventLoop::EventLoop()
	: mStarted(true)
	, mEpollFd(::epoll_create1(EPOLL_CLOEXEC))
{
	mLogger = RegisterLogger("EventLoop");

	if(mEpollFd < 0)
	{
		mLogger->critical("Failed to setup epoll interface");
		throw std::runtime_error("Failed to seetup epoll interface");
	}

	io_uring_queue_init(MaxIORingQueueEntries, &mIoUring, IORING_SETUP_IOPOLL);

	SetupSignalWatcher();
}

void EventLoop::Configure()
{
	auto config = GetConfigTable("EventLoop");
	if(!config)
	{
		throw std::runtime_error("Failed to load config table");
	}

	auto statInterval = config->get_as<int>("StatInterval");
	if(statInterval)
	{
		mStatTimerInterval = *statInterval;
		EnableStatistics();
	}

	mRunHot = config->get_as<bool>("RunHot").value_or(false);
	if(!mRunHot)
	{
		mEpollTimeout = 10;
	}
	else
	{
		mEpollTimeout = 0;
	}
}

int EventLoop::Run()
{
	mStatsTime = std::chrono::high_resolution_clock::now();
	mLogger->info("Eventloop has started");
	while (true)
	{
		/**
		 * @brief epoll section
		 *
		 * This section handles all FD operations that can be polled by epoll
		 */
		mEpollReturn = ::epoll_wait(mEpollFd, mEpollEvents, MaxEpollEvents, mEpollTimeout);
		mLogger->trace("epoll_wait returned: {}", mEpollReturn);
		if(mEpollReturn < 0)
		{
			mLogger->critical("Error on epoll");
			throw std::runtime_error("Error on epoll");
		}
		else
		{
			mLogger->trace("{} events on fd's", mEpollReturn);
			for(int event = 0; event < mEpollReturn; ++event)
			{
				if (mEpollEvents[event].events & EPOLLERR ||
					mEpollEvents[event].events & EPOLLHUP) /*||
					!(mEpollEvents[event].events & EPOLLIN) ||
					!(mEpollEvents[event].events & EPOLLOUT))*/ // error
				{
					mLogger->error("epoll event error, fd:{}, event:{}, errno:{}", mEpollEvents[event].data.fd, mEpollEvents[event].events, errno);
					close(mEpollEvents[event].data.fd);
				}
				else if(mEpollEvents[event].events & EPOLLIN)
				{
					if(mEpollEvents[event].data.fd == mSignalFd)
					{
						const size_t s = ::read(mSignalFd, &mFDSI, sizeof(struct signalfd_siginfo));
						if(s != sizeof(struct signalfd_siginfo))
						{
							mLogger->critical("Error reading signal fd:{}, errno:{}", mSignalFd, errno);
							throw std::runtime_error("Error reading signalfd");
						}

						if(mFDSI.ssi_signo == SIGINT)
						{
							mLogger->info("Got SIGINT, shutting down application");
							return 0;
						}
						else if(mFDSI.ssi_signo == SIGQUIT)
						{
							mLogger->info("Got SIGQUIT, shutting down application");
							return 0;
						}
					}
					else
					{
						mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorRead(mEpollEvents[event].data.fd);
					}
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
					mLogger->warn("Unhandled event:{} on fd:{}", mEpollEvents[event].data.fd, mEpollEvents[event].events);
				}
			}
		}

		/**
		 * @brief io_uring section
		 *
		 * Section for handling io_uring polling
		 */
		const int ioRingReturn = io_uring_enter(mIoUring.ring_fd, 0, 0, IORING_ENTER_GETEVENTS, 0);
		if(!ioRingReturn)
		{
			throw std::runtime_error("Error polling ioring");
		}

		for(const auto& [callback, latencyClass] : mCallbacks)
		{
			/**
			 * Two different methods are available for scheduling timers with a high latency.
			 *
			 * The problem with the first one is that it requires modulo operations.
			 * These are slow.
			 *
			 * The second one attempts to circumvent this by using a simple counter.
			 * Initial testing seems to indicate that both have an equal effect on the cycles per second count.
			 */
			callback->OnEventLoopCallback();
			/*
			if(latencyClass == LatencyType::Low)
			{
				callback->OnEventLoopCallback();
			}
			else if(latencyClass == LatencyType::High && mCycleCount % 1000)
			{
				callback->OnEventLoopCallback();
			}
			*/
			/*
			if(latencyClass == LatencyType::Low)
			{
				callback->OnEventLoopCallback();
			}
			else
			{
				if(mTimerIterationCounter == 1000)
				{
					callback->OnEventLoopCallback();
					mTimerIterationCounter = 0;
				}
				else
				{
					++mTimerIterationCounter;
				}
			}
			*/
		}

		for(const auto& timer : mTimers)
		{
			if(timer->CheckTimerExpired())
			{
				timer->mCallback();
				//mLogger->info("Timer has expired after {}", std::chrono::seconds(timer.mDuration).count());
				if(timer->mType == TimerType::Oneshot)
				{
					mTimers.erase(std::remove(std::begin(mTimers), std::end(mTimers), timer), std::end(mTimers));
				}
				else
				{
					timer->UpdateDeadline();
				}
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
	struct epoll_event event{};
	event.data.fd = fd;
	event.events = events;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) == -1)
	{
		mLogger->critical("Failed to add fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to add fd to epoll interface");
	}
	mFdHandlers.insert({fd, handler});
	mLogger->info("Registered Fd: {}", fd);
}

void EventLoop::ModifyFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler)
{
	struct epoll_event event{};
	event.data.fd = fd;
	event.events = events;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_MOD, fd, &event) == -1)
	{
		mLogger->critical("Failed to mod fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to mod fd to epoll interface");
	}
	mFdHandlers.insert({fd, handler});
	mLogger->info("Modified Fd: {}", fd);
}

void EventLoop::UnregisterFiledescriptor(int fd)
{
	struct epoll_event event{};
	event.data.fd = fd;
	event.events = 0;
	if (epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, &event) == -1)
	{
		mLogger->critical("Failed to del fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to del fd to epoll interface");
	}

	const auto lookup = mFdHandlers.find(fd);
	mFdHandlers.erase(lookup);

	mLogger->info("Unregistered Fd: {}", fd);
}

bool EventLoop::IsRegistered(const int fd)
{
	const auto lookup = mFdHandlers.find(fd);
	if(lookup == mFdHandlers.end())
	{
		return false;
	}
	return true;
}

void EventLoop::EnableStatistics() noexcept
{
	mStatsTimer = Timer(static_cast<std::chrono::seconds>(mStatTimerInterval), TimerType::Repeating, [this](){ PrintStatistics(); });
	AddTimer(&mStatsTimer);
	mStatistics = true;
}

void EventLoop::PrintStatistics() noexcept
{
	auto interval = std::chrono::high_resolution_clock::now() - mStatsTime;

	mLogger->info("EventLoop statistics -> Cycles: {} Interval: {}ms",
			mCycleCount,
			std::chrono::duration_cast<std::chrono::milliseconds>(interval).count());

	mCycleCount = 0;
	mStatsTime = std::chrono::high_resolution_clock::now();
}

void EventLoop::SetupSignalWatcher()
{
	::sigemptyset(&mSigMask);
	::sigaddset(&mSigMask, SIGINT);
	::sigaddset(&mSigMask, SIGQUIT);

	if(::sigprocmask(SIG_BLOCK, &mSigMask, NULL) == -1)
	{
		mLogger->critical("Failed to block other signal handlers, errno:{}", errno);
		throw std::runtime_error("Failed to block other signal handlers");
	}

	mSignalFd = ::signalfd(-1, &mSigMask, SFD_NONBLOCK|SFD_CLOEXEC);

	if(mSignalFd == -1)
	{
		mLogger->critical("Failed to create signal watcher, errno:{}", errno);
		throw std::runtime_error("Failed to create signal watcher");
	}

	struct epoll_event event{};
	event.data.fd = mSignalFd;
	event.events = EPOLLIN;
	if(::epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mSignalFd, &event) == -1)
	{
		mLogger->critical("Failed to add signalFd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to add signalFd to epoll interface");
	}

}

void EventLoop::LoadConfig(const std::string& configFile) noexcept
{
	mConfig = cpptoml::parse_file(configFile);
}

void EventLoop::SheduleForNextCycle(const std::function<void()> func) noexcept
{
	mShortTimers.push_back(Timer(0s, TimerType::Oneshot, func));
	AddTimer(&mShortTimers.back());
}

std::shared_ptr<spdlog::logger> EventLoop::RegisterLogger(const std::string& module) const noexcept
{
	std::shared_ptr<spdlog::logger> logger = spdlog::get(module);
	if(logger == nullptr)
	{
		auto newLogger = spdlog::stdout_color_mt(module);
		logger = spdlog::get(module);
	}

	return logger;
}

std::shared_ptr<cpptoml::table> EventLoop::GetConfigTable(const std::string& module) const noexcept
{
	const auto table = mConfig->get_table(module);

	if(!table)
	{
		mLogger->critical("Failed to load config table for module: {}", module);
		throw std::runtime_error("Failed to load config table");
	}

	return table;
}

}
