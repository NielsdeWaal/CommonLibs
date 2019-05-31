#ifndef EXAMPLE_H
#define EXAMPLE_H

#include "EventLoop.h"

using namespace std::chrono_literals;

class ExampleApp : public EventLoop::IEventLoopCallbackHandler
{
public:
	ExampleApp(EventLoop::EventLoop& ev)
		: mEv(ev)
		, mTimer(1s, EventLoop::EventLoop::TimerType::Repeating, [this](){ OnTimerCallback(); })
	{
		//mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
	}

	~ExampleApp() {}

	void Initialise()
	{
		mEv.AddTimer(&mTimer);
	}

	void OnTimerCallback()
	{
		spdlog::info("Got callback from timer");
	}

	void OnEventLoopCallback() final
	{
		spdlog::info("Got callback from EV");
	}

private:
	EventLoop::EventLoop& mEv;
	EventLoop::EventLoop::Timer mTimer;

	int mFd = 0;
	char mSockBuf[100];
	struct addrinfo hints, *servinfo, *p;

	
};

#endif // EXAMPLE_H
