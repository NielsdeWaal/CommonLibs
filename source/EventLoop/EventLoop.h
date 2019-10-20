#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <chrono>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cpptoml.h>

#include "Common/NonCopyable.h"

namespace EventLoop {

using namespace std::chrono_literals;

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
	virtual ~IEventLoopCallbackHandler() {}
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
	virtual ~IFiledescriptorCallbackHandler() {}
};

class EventLoop
	: Common::NonCopyable<EventLoop>
{
public:
	EventLoop();

	int Run();
	void Stop();

	enum class TimerType : std::uint8_t {
		Oneshot = 0,
		Repeating = 1
	};

	enum class TimerState : std::uint8_t {
		Idle = 0,
		Active = 1
	};

	//TODO Be able to update timer
	struct Timer
	{
		using TimePoint = std::chrono::high_resolution_clock::time_point;
		using Clock = std::chrono::high_resolution_clock;
		using seconds = std::chrono::seconds;

		Timer(seconds duration, TimerType type, std::function<void()> callback)
			: mEnd(static_cast<TimePoint>(Clock::now()) + duration)
			, mState(TimerState::Active)
			, mDuration(duration)
			, mType(type)
			, mCallback(callback)
		{}

		Timer() = default;

		bool CheckTimerExpired() const noexcept
		{
			return (static_cast<TimePoint>(Clock::now()) > mEnd) ? true : false;
		}

		void UpdateDeadline() noexcept
		{
			mEnd = static_cast<TimePoint>(Clock::now()) + mDuration;
		}

		bool operator==(const Timer& rhs) const noexcept
		{
			return mEnd == rhs.mEnd;
		}

		TimePoint mEnd;
		TimerState mState;
		seconds mDuration;
		TimerType mType;
		std::function<void()> mCallback;
	};

	//TODO Add RemoveTimer function
	void AddTimer(Timer* timer);

	enum class LatencyType : std::uint8_t {
		Low = 0,
		High = 1
	};

	void Configure();

	void RegisterCallbackHandler(IEventLoopCallbackHandler* callback, LatencyType latency);
	void RegisterFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler);
	void ModifyFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler);
	void UnregisterFiledescriptor(int fd);
	bool IsRegistered(const int fd);

	void EnableStatistics() noexcept;

	void SheduleForNextCycle(const std::function<void()> func) noexcept;

	void LoadConfig(const std::string& configFile) noexcept;

	std::shared_ptr<spdlog::logger> RegisterLogger(const std::string& module) const noexcept;
	std::shared_ptr<cpptoml::table> GetConfigTable(const std::string& module) const noexcept;

private:
	void PrintStatistics() noexcept;

	void SetupSignalWatcher();

	static constexpr int MaxEpollEvents = 64;

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
	struct epoll_event mEpollEvents[MaxEpollEvents];
	std::unordered_map<int, IFiledescriptorCallbackHandler*> mFdHandlers;
	int mEpollTimeout = 0;
	// void CleanupTimers();
	// Single timer class with enum state dictating if timer is repeating or not

	int mSignalFd = 0;
	sigset_t mSigMask;
	struct signalfd_siginfo mFDSI;

	//int mTimerIterationCounter = 0;

	std::shared_ptr<cpptoml::table> mConfig;
	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace EventLoop

#endif // EVENTLOOP_H
