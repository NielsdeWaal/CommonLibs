#include "EventLoop.h"

#include "TSC.h"
#include "UringCommands.h"
#include "Util.h"
#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <memory>

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

	// int ret = io_uring_queue_init(MaxIORingQueueEntries, &mIoUring, IORING_SETUP_IOPOLL);
	// int uringFlags = IORING_SETUP_IOPOLL;
	// int ret = io_uring_queue_init(MaxIORingQueueEntries, &mIoUring, uringFlags);
	int ret = io_uring_queue_init(MaxIORingQueueEntries, &mIoUring, 0);
	if(ret < 0)
	{
		mLogger->critical("Failed to setup io_uring, ret: {}", ret);
		throw std::runtime_error("Failed to setup io_uring");
	}

	// io_uring_params uringParams{};
	// uringParams.flags |= IORING_SETUP_IOPOLL;
	// io_uring_setup(MaxIORingQueueEntries, &uringParams);

	SetupSignalWatcher();

	mFdHandlers.reserve(FdHandlerReserve);
	std::fill(mFdHandlers.begin(), mFdHandlers.end(), nullptr);
}

EventLoop::~EventLoop()
{
	io_uring_queue_exit(&mIoUring);
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
		mEpollTimeout = NonRunHotEpollTimeout;
	}
	else
	{
		mEpollTimeout = 0;
	}
}

void EventLoop::Stop()
{
	mStopped = true;
}

int EventLoop::Run()
{
	PROFILING_ZONE_NAMED("Run function");
	mStatsTime = std::chrono::high_resolution_clock::now();
	mLogger->info("Eventloop has started");
	while(true)
	{
		PROFILING_ZONE_NAMED("Main run loop");
		mEpollReturn = ::epoll_wait(mEpollFd, mEpollEvents.data(), MaxEpollEvents, mEpollTimeout);
		mLogger->trace("epoll_wait returned: {}", mEpollReturn);
		if(mEpollReturn < 0)
		{
			mLogger->critical("Error on epoll");
			throw std::runtime_error("Error on epoll");
		}

		PROFILING_ZONE_NAMED("Handling events on epoll");
		mLogger->trace("{} events on fd's", mEpollReturn);
		for(int event = 0; event < mEpollReturn; ++event)
		{
			if (mEpollEvents[event].events & EPOLLERR ||
					mEpollEvents[event].events & EPOLLHUP) /*||
					!(mEpollEvents[event].events & EPOLLIN) ||
					!(mEpollEvents[event].events & EPOLLOUT))*/ // error
			{
				const int& fd = mEpollEvents[event].data.fd;
				const uint32_t& events = mEpollEvents[event].events;
				mLogger->error("epoll event error, fd:{}, event:{}, errno:{}", fd, events, errno);
				::close(mEpollEvents[event].data.fd);
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
					PROFILING_ZONE_NAMED("Calling OnFiledescriptorRead handler");
					mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorRead(mEpollEvents[event].data.fd);
				}
			}
			else if(mEpollEvents[event].events & EPOLLOUT)
			{
				PROFILING_ZONE_NAMED("Calling OnFiledescriptorWrite handler");
				mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorWrite(mEpollEvents[event].data.fd);
			}
			// else if(mEpollEvents[event].events & EPOLLHUP)
			//{
			//	mFdHandlers[mEpollEvents[event].data.fd]->OnFiledescriptorWrite(mEpollEvents[event].data.fd);
			//}
			else
			{
				const int& fd = mEpollEvents[event].data.fd;
				const uint32_t& events = mEpollEvents[event].events;
				mLogger->warn("Unhandled event:{} on fd:{}", fd, events);
			}
		}

		/**
		 * @brief io_uring section
		 *
		 * Section for handling io_uring polling
		 *
		 * TODO Should probably move to something like a `peek` function
		 */
		const int submitRet = io_uring_submit(&mIoUring);
		::io_uring_cqe* cqe = nullptr;
		const int peekRet = io_uring_peek_cqe(&mIoUring, &cqe);
		if(peekRet == 0)
		{
			mLogger->info("Got uring completion event, res: {}", cqe->res);
			if(cqe->res < 0)
			{
				mLogger->critical("Got error on completion event: {}", cqe->res);
			}
			else
			{
				// auto* data = reinterpret_cast<UserData*>(cqe->user_data);
				auto data = static_cast<UserData*>(io_uring_cqe_get_data(cqe));
				if(data)
				{
					if(data->mHandleType == HandleType::Standard) {
						data->mCallback->OnCompletion(*cqe, data);
					} else if(data->mHandleType == HandleType::Coroutine) {
						data->mResolver->resolve(cqe->res);
					}
				}
				else
				{
					mLogger->critical("received nullptr user_data on completion");
				}
			}
			io_uring_cqe_seen(&mIoUring, cqe);
		}
		// if(peekRet != 0)
		// {
		// unsigned completed = 0;

		// unsigned head = 0;
		// ::io_uring_cqe* cqe = nullptr;
		// io_uring_for_each_cqe(&mIoUring, head, cqe)
		// {
		// 	completed++;
		// 	mLogger->info("Received cqe");
		// 	if(cqe->res < 0)
		// 	{
		// 		mLogger->error("Error in completion event: {}", cqe->res);
		// 	}
		// }
		// io_uring_cq_advance(&mIoUring, completed);
		// }

		/**
		 * TODO There seems to be some room for improvement here.
		 * I think we could optimise this slightly by just storing structs in a vector.
		 * This should provide better iteration speed, especially when we reserve a good chunk of memory
		 */
		for(const auto& [callback, latencyClass]: mCallbacks)
		{
			PROFILING_ZONE_NAMED("Callbacks");
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

		// TODO cache current time value, no need to retrieve it at every cycle
		for(const auto& timer: mTimers)
		{
			PROFILING_ZONE_NAMED("Timers");
			const Common::MONOTONIC_TIME now = Common::MONOTONIC_CLOCK::Now();
			if(timer->CheckTimerExpired(now))
			{
				timer->mCallback();
				// mLogger->info("Timer has expired after {}", std::chrono::seconds(timer.mDuration).count());
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

		if(mStopped)
		{
			return 0;
		}

		mCycleCount++;
		PROFILING_FRAME();
	}
}

void EventLoop::RegisterFile(int fd)
{}

void EventLoop::AddTimer(Timer* timer)
{
	mTimers.push_back(timer);
}

void EventLoop::RemoveTimer(Timer* timer)
{
	auto it = std::find_if(mTimers.begin(), mTimers.end(), [&](Timer* t) {
		return timer == t;
	});
	if(it != mTimers.end())
	{
		mTimers.erase(it);
	}
}

void EventLoop::RegisterCallbackHandler(IEventLoopCallbackHandler* callback, LatencyType latency)
{
	mCallbacks.insert({callback, latency});
}

void EventLoop::RegisterFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler)
{
	::epoll_event event{};
	event.data.fd = fd;
	event.events = events;
	if(epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) == -1)
	{
		mLogger->critical("Failed to add fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to add fd to epoll interface");
	}
	// mFdHandlers.insert({fd, handler});
	// mLogger->info("Registered Fd: {}", fd);

	// io_uring_sqe* sqe = io_uring_get_sqe(&mIoUring);
	// io_uring_prep_poll_add(sqe, fd, events);

	// io_uring_submit(&mIoUring);

	mFdHandlers[fd] = handler;
	mLogger->info("Registered Fd: {}", fd);
}

void EventLoop::ModifyFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler)
{
	::epoll_event event{};
	event.data.fd = fd;
	event.events = events;
	if(epoll_ctl(mEpollFd, EPOLL_CTL_MOD, fd, &event) == -1)
	{
		mLogger->critical("Failed to mod fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to mod fd to epoll interface");
	}
	mFdHandlers[fd] = handler;
	mLogger->info("Modified Fd: {}", fd);
}

void EventLoop::UnregisterFiledescriptor(int fd)
{
	struct epoll_event event
	{
	};
	event.data.fd = fd;
	event.events = 0;
	if(epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, &event) == -1)
	{
		mLogger->critical("Failed to del fd to epoll interface, errno:{}", errno);
		throw std::runtime_error("Failed to del fd to epoll interface");
	}

	mLogger->info("Unregistered Fd: {}", fd);
}

bool EventLoop::IsRegistered(const int fd)
{
	return mFdHandlers[fd] == nullptr;
}

void EventLoop::EnableStatistics() noexcept
{
	mStatsTimer = Timer(Common::MONOTONIC_CLOCK::ToCycles(mStatTimerInterval), TimerType::Repeating, [this]() {
		PrintStatistics();
	});
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

	if(::sigprocmask(SIG_BLOCK, &mSigMask, nullptr) == -1)
	{
		mLogger->critical("Failed to block other signal handlers, errno:{}", errno);
		throw std::runtime_error("Failed to block other signal handlers");
	}

	mSignalFd = ::signalfd(-1, &mSigMask, SFD_NONBLOCK | SFD_CLOEXEC);

	if(mSignalFd == -1)
	{
		mLogger->critical("Failed to create signal watcher, errno:{}", errno);
		throw std::runtime_error("Failed to create signal watcher");
	}

	struct epoll_event event
	{
	};
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

void EventLoop::SheduleForNextCycle(std::function<void()> func) noexcept
{
	using namespace Common::literals;
	mShortTimers.emplace_back(0_s, TimerType::Oneshot, std::move(func));
	AddTimer(&mShortTimers.back());
}

void EventLoop::QueueStandardRequest(std::unique_ptr<UserData> userData)
{
	mLogger->info("Queueing standard request");
	// std::unique_ptr<SubmissionQueueEvent> evt = std::make_unique<SubmissionQueueEvent>(io_uring_get_sqe(&mIoUring));
	SubmissionQueueEvent* evt = io_uring_get_sqe(&mIoUring);
	if(evt == nullptr)
	{
		mLogger->critical("Unable to get new sqe from io_uring");
		return;
	}

	UserData* data = userData.release();

	FillSQE(evt, data->mType, data);

	io_uring_sqe_set_data(evt, data);

	int ret = io_uring_submit(&mIoUring);
	if(ret < 0)
	{
		mLogger->critical("Unable to submit request to io_uring");
	}
}

void EventLoop::FillSQE(SubmissionQueueEvent* sqe, const SourceType& data, const UserData* userData)
{
	switch(data)
	{
	case SourceType::Nop: {
		mLogger->info("Prepping nop request");
		io_uring_prep_nop(sqe);
		break;
	}
	case SourceType::Open: {
		mLogger->info("Prepping open request");
		const auto& openData = std::get<OPEN>(userData->mInfo);
		io_uring_prep_openat(sqe, AT_FDCWD, openData.filename->c_str(), openData.flags, openData.mode);
		break;
	}
	case SourceType::Close: {
		mLogger->info("Prepping close request");
		const auto& openData = std::get<CLOSE>(userData->mInfo);
		io_uring_prep_close(sqe, openData.fd);
		break;
	}
	case SourceType::Read: {
		mLogger->info("Prepping read request");
		const auto& openData = std::get<READ>(userData->mInfo);
		io_uring_prep_read(sqe, openData.fd, openData.buf, openData.len, openData.pos);
		break;
	}
	case SourceType::Write: {
		mLogger->info("Prepping write request");
		const auto& openData = std::get<WRITE>(userData->mInfo);
		io_uring_prep_write(sqe, openData.fd, openData.buf, openData.len, openData.pos);
		break;
	}
	default: {
		throw std::runtime_error("Unhandled SourceType");
		break;
	}
	}
}

SqeAwaitable EventLoop::SubmitRead(int fd, std::uint64_t pos, void* buf, std::size_t len)
{
	SubmissionQueueEvent* evt = io_uring_get_sqe(&mIoUring);
	if(evt == nullptr)
	{
		mLogger->critical("Unable to get new sqe from io_uring");
		assert(false);
	}

	mLogger->info("Creating read coroutine, for fd: {}", fd);
	// io_uring_sqe_set_data(evt, new UserData{.mHandleType = HandleType::Coroutine, .mType = SourceType::Read});

	io_uring_prep_read(evt, fd, buf, len, pos);

	return AwaitWork(evt, 0);
}

SqeAwaitable EventLoop::SubmitWrite(int fd, const void* buf, std::size_t len, std::size_t offset)
{
	SubmissionQueueEvent* evt = io_uring_get_sqe(&mIoUring);
	if(evt == nullptr)
	{
		mLogger->critical("Unable to get new sqe from io_uring");
		assert(false);
	}

	mLogger->info("Creating write coroutine, for fd: {}", fd);
	// io_uring_sqe_set_data(evt, new UserData{.mHandleType = HandleType::Coroutine, .mType = SourceType::Read});

	io_uring_prep_write(evt, fd, buf, len, offset);

	return AwaitWork(evt, 0);
}

SqeAwaitable EventLoop::SubmitClose(int fd)
{
	SubmissionQueueEvent* evt = io_uring_get_sqe(&mIoUring);
	if(evt == nullptr)
	{
		mLogger->critical("Unable to get new sqe from io_uring");
		assert(false);
	}

	mLogger->info("Creating close coroutine, for fd: {}", fd);
	// io_uring_sqe_set_data(evt, new UserData{.mHandleType = HandleType::Coroutine, .mType = SourceType::Read});

	io_uring_prep_close(evt, fd);

	return AwaitWork(evt, 0);
	}

SqeAwaitable EventLoop::SubmitOpenAt(const char* path, int flags, mode_t mode)
{
	SubmissionQueueEvent* evt = io_uring_get_sqe(&mIoUring);
	if(evt == nullptr)
	{
		mLogger->critical("Unable to get new sqe from io_uring");
		assert(false);
	}

	mLogger->info("Creating open coroutine");
	// io_uring_sqe_set_data(evt, new UserData{.mHandleType = HandleType::Coroutine, .mType = SourceType::Open});

	io_uring_prep_openat(evt, AT_FDCWD, path, flags, mode);
	// int uringRet = io_uring_submit(&mIoUring);
	return AwaitWork(evt, 0);
}

SqeAwaitable EventLoop::AwaitWork(SubmissionQueueEvent* evt, std::uint8_t iflags)
{
	io_uring_sqe_set_flags(evt, iflags);

	return SqeAwaitable(evt);
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

std::shared_ptr<cpptoml::table> EventLoop::GetConfigTable(const std::string& module) const
{
	auto table = mConfig->get_table(module);

	if(!table)
	{
		mLogger->critical("Failed to load config table for module: {}", module);
		throw std::runtime_error("Failed to load config table");
	}

	return table;
}

} // namespace EventLoop
