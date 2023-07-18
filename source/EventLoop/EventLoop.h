#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <chrono>
#include <functional>
#include <unordered_map>
#include <variant>
#include <vector>

#include <csignal>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <liburing.h>
#include <linux/io_uring.h>

// #include <spdlog/fmt/bin_to_hex.h>
#include <cpptoml.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "DmaBuffer.h"
#include "NonCopyable.h"
#include "TSC.h"
#include "UringCommands.h"

namespace EventLoop {

using namespace std::chrono_literals;

template<class... Ts>
struct overloaded : Ts...
{
	using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

/**
 * @brief virtual class for eventloop callback
 *
 * Inherit this virtual class when a callback has to be registerd in the eventloop.
 * This callback will be called at a rate depending on the LatencyType.
 *
 * Register the callback using the RegisterCallbackHandler() function.
 */
class IEventLoopCallbackHandler
{
public:
	virtual void OnEventLoopCallback() = 0;
	virtual ~IEventLoopCallbackHandler() = default;
	// IEventLoopCallbackHandler(const IEventLoopCallbackHandler&) = delete;
	// IEventLoopCallbackHandler(const IEventLoopCallbackHandler&&) = delete;
	// IEventLoopCallbackHandler& operator=(const IEventLoopCallbackHandler&) = delete;
	// IEventLoopCallbackHandler& operator=(const IEventLoopCallbackHandler&&) = delete;
};

/**
 * @brief Callback handler for filedescriptors
 *
 * Each overload function will be called upon the relevant event.
 * Events can be registerd using the RegisterFiledescriptor and ModifyFiledescriptor functions.
 */
class IFiledescriptorCallbackHandler
{
public:
	virtual void OnFiledescriptorRead(int fd) = 0;
	virtual void OnFiledescriptorWrite(int fd) = 0;
	virtual ~IFiledescriptorCallbackHandler() = default;
};

/**
 * @brief The core eventloop for the commonlibs framework.
 *
 * This framework is build on the low latency requirements that certain projects need with options for more conservative
 * scheduling. We use standard linux subsystems such as io_uring and epoll to achieve low latency IO.
 *
 * Future expansion:
 * - Different io_uring rings (low latency/standard requests).
 * - Deamonise the application.
 * - Bind application to a single core.
 */
class EventLoop : Common::NonCopyable<EventLoop>
{
public:
	EventLoop();
	~EventLoop();

	int Run();
	void Stop();

	enum class TimerType : std::uint8_t
	{
		Oneshot = 0,
		Repeating = 1
	};

	enum class TimerState : std::uint8_t
	{
		Idle = 0,
		Active = 1
	};

	// TODO Be able to update timer
	// TODO Remove std::function in favor of template argument
	// TODO Move timing to template instead of variant depending on whether we want to be able to update between
	// timescales
	struct Timer
	{
		Timer() = default;

		Timer(Common::MONOTONIC_TIME deadline, TimerType type, std::function<void()> callback)
			: mEnd(Common::MONOTONIC_CLOCK::Now() + deadline)
			, mType(type)
			, mState(TimerState::Active)
			, mCallback(std::move(callback))
			, mDuration(deadline)
		{}

		[[nodiscard]] bool CheckTimerExpired() const noexcept
		{
			return Common::MONOTONIC_CLOCK::Now() > mEnd;
		}

		[[nodiscard]] bool CheckTimerExpired(Common::MONOTONIC_TIME now) const noexcept
		{
			return now > mEnd;
		}

		void UpdateDeadline() noexcept
		{
			mEnd = Common::MONOTONIC_CLOCK::Now() + mDuration;
		}

		// private:
		Common::MONOTONIC_TIME mEnd;
		TimerType mType;
		TimerState mState;
		std::function<void()> mCallback;
		Common::MONOTONIC_TIME mDuration;
	};

	// TODO Add RemoveTimer function
	void AddTimer(Timer* timer);
	void RemoveTimer(Timer* timer);

	enum class LatencyType : std::uint8_t
	{
		Low = 0,
		High = 1
	};

	void Configure();

	/// EPOLL related operations
	void RegisterCallbackHandler(IEventLoopCallbackHandler* callback, LatencyType latency);
	void RegisterFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler);
	void ModifyFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler);
	void UnregisterFiledescriptor(int fd);
	bool IsRegistered(int fd);

	/// io_uring related operations
	// TODO maybe add openat call, this could also be used as a generic open file (openat(0))
	void RegisterBuffers(const ::iovec* iovecs, std::size_t amount);
	void RegisterFile(int fd);
	void SubmitWritev();
	void SubmitReadv();
	SqeAwaitable SubmitRead(int fd, std::uint64_t pos, void* buf, std::size_t len);
	SqeAwaitable SubmitOpenAt(const char* path, int flags, mode_t mode);
	SqeAwaitable SubmitWrite(int fd, const void* buf, std::size_t len, std::size_t offset);
	SqeAwaitable SubmitClose(int fd);
	SqeAwaitable SubmitStatx(int fd, struct statx* st);

	// TODO
	// For now this just using aligned mallocs, maybe change this
	// to use a memory pool with preallocated memory which has to be released
	// back by the caller
	DmaBuffer AllocateDmaBuffer(std::size_t size);

	/**
	 * @brief Queue a standard io_uring request to the ring
	 *
	 * This function will be expanded in the future where it can be used to differentiate between
	 * direct IO and normal requests such as Send/Recv.
	 */
	void QueueStandardRequest(std::unique_ptr<UserData>, int flags = 0);

	SqeAwaitable AwaitWork(SubmissionQueueEvent* evt, std::uint8_t iflags);

	void EnableStatistics() noexcept;

	void SheduleForNextCycle(std::function<void()> func) noexcept;

	void LoadConfig(const std::string& configFile) noexcept;

	std::shared_ptr<spdlog::logger> RegisterLogger(const std::string& module) const noexcept;
	std::shared_ptr<cpptoml::table> GetConfigTable(const std::string& module) const;

	struct ConnInfo
	{
	public:
		int mFD;
		int mType;
	};

private:
	/**
	 * @brief Pass SubmissionQueueEvent and user data to be submitted to the ring
	 *
	 */
	void FillSQE(SubmissionQueueEvent* sqe, const SourceType& data, const UserData* userData);

	void PrintStatistics() noexcept;

	void SetupSignalWatcher();

	static constexpr int MaxIORingQueueEntries = 64;
	static constexpr int MaxEpollEvents = 64;
	static constexpr int FdHandlerReserve = 512;
	static constexpr int NonRunHotEpollTimeout = 10;

	bool mStarted;
	bool mStatistics;
	int mStatTimerInterval;
	Timer mStatsTimer;
	long mCycleCount = 0;
	std::chrono::high_resolution_clock::time_point mStatsTime;
	bool mRunHot;

	std::vector<Timer*> mTimers;

	// Need to change mTimers to use unique_ptr's.
	// This way we can get rid of this backup vector
	std::vector<Timer> mShortTimers;
	std::unordered_map<IEventLoopCallbackHandler*, LatencyType> mCallbacks;

	const int mEpollFd = 0;
	int mEpollReturn = 0;
	std::array<struct epoll_event, MaxEpollEvents> mEpollEvents;
	std::vector<IFiledescriptorCallbackHandler*> mFdHandlers;
	int mEpollTimeout = 0;
	// void CleanupTimers();
	// Single timer class with enum state dictating if timer is repeating or not

	int mSignalFd = 0;
	sigset_t mSigMask;
	struct signalfd_siginfo mFDSI;

	// int mTimerIterationCounter = 0;

	struct BufferConfig {
		std::size_t bufSize;
		std::size_t bufCount;
	};
	io_uring mIoUring;
	// std::vector<std::pair<std::size_t, std::size_t>> mBufferConfig = {{128 * 1024 * 1024, 4}, {4096, 64}};
	// std::vector<BufferConfig> mBufferConfig = {{128 * 1024 * 1024, 4}, {4096, 64}};
	// std::vector<BufferConfig> mBufferConfig;
	// std::vector<io_uring_buf_ring*> mBufferRings;
	// std::vector<std::unique_ptr<char[]>> mBuffers;

	bool mStopped = false;

	std::shared_ptr<cpptoml::table> mConfig;
	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace EventLoop

#endif // EVENTLOOP_H
