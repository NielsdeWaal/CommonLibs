#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <chrono>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Common/NonCopyable.h"

namespace EventLoop {

using namespace std::chrono_literals;


class IEventLoopCallbackHandler
{
public:
	virtual void OnEventLoopCallback() = 0;
	virtual ~IEventLoopCallbackHandler() {}
};

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

	void Run();
	void Stop();

	enum class TimerType : std::uint8_t {
		Oneshot = 0,
		Repeating = 1
	};

	enum class TimerState : std::uint8_t {
		Idle = 0,
		Active = 1
	};

	struct Timer
	{
		using TimePoint = std::chrono::steady_clock::time_point;
		using Clock = std::chrono::steady_clock;
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

	void AddTimer(Timer* timer);

	enum class LatencyType : std::uint8_t {
		Low = 0,
		High = 1
	};

	void RegisterCallbackHandler(IEventLoopCallbackHandler* callback, LatencyType latency);
	void RegisterFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler);
	void ModifyFiledescriptor(int fd, uint32_t events, IFiledescriptorCallbackHandler* handler);
	void UnregisterFiledescriptor(int fd);

	void EnableStatistics() noexcept;

private:
	void PrintStatistics() noexcept;

	static constexpr int MaxEpollEvents = 64;

	bool mStarted;
	bool mStatistics;
	Timer mStatsTimer;
	long mCycleCount = 0;

	std::vector<Timer*> mTimers;
	std::unordered_map<IEventLoopCallbackHandler*, LatencyType> mCallbacks;

	int mEpollFd = 0;
	int mEpollReturn = 0;
	struct epoll_event mEpollEvents[MaxEpollEvents];
	std::unordered_map<int, IFiledescriptorCallbackHandler*> mFdHandlers;
	// void CleanupTimers();
	// Single timer class with enum state dictating if timer is repeating or not

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // EVENTLOOP_H
