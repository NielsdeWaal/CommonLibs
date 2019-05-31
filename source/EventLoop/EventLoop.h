#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <chrono>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

#include <spdlog/spdlog.h>

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
	virtual void OnFiledescriptorRead() = 0;
	virtual void OnFiledescriptorWrite() = 0;
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

		bool CheckTimerExpired()
		{
			return (static_cast<TimePoint>(Clock::now()) > mEnd) ? true : false;
		}

		void UpdateDeadline()
		{
			mEnd = static_cast<TimePoint>(Clock::now()) + mDuration;
		}

		bool operator==(const Timer& rhs)
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
	void RegisterFiledescriptor(int fd, uint32_t events);

	void EnableStatistics();

private:
	void PrintStatistics();

	static constexpr int MaxEpollEvents = 64;

	bool mStarted;
	bool mStatistics;
	Timer mStatsTimer;
	long mCycleCount = 0;

	std::vector<Timer*> mTimers;
	std::unordered_map<IEventLoopCallbackHandler*, LatencyType> mCallbacks;

	int mEpollFd = 0;
	struct epoll_event mEpollEvents[MaxEpollEvents];
	// void CleanupTimers();
	// Single timer class with enum state dictating if timer is repeating or not
};

}

#endif // EVENTLOOP_H
