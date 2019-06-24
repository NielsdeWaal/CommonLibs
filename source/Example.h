#ifndef EXAMPLE_H
#define EXAMPLE_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "EventLoop.h"

#include "StreamSocket.h"

using namespace std::chrono_literals;

class ExampleApp : public EventLoop::IEventLoopCallbackHandler
				 , public Common::IStreamSocketHandler
				 , public Common::IStreamSocketServerHandler
{
public:
	ExampleApp(EventLoop::EventLoop& ev)
		: mEv(ev)
		, mTimer(1s, EventLoop::EventLoop::TimerType::Repeating, [this](){ OnTimerCallback(); })
		, mSocket(mEv, this)
		, mServer(mEv, this)
	{
		//mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
	}

	~ExampleApp() {}

	void Initialise()
	{
		//mEv.AddTimer(&mTimer);
		mSocket.Connect("127.0.0.1", 1337);
		mServer.BindAndListen(1338);
	}

	void OnTimerCallback()
	{
		spdlog::info("Got callback from timer");
	}

	void OnEventLoopCallback() final
	{
		spdlog::info("Got callback from EV");
	}

	void OnConnected() final
	{
		spdlog::info("Connection succeeded");
	}

	void OnDisconnect() final
	{
		spdlog::info("Connection terminated");
	}

	void OnIncomingData(Common::StreamSocket* conn, char* data, size_t len) final
	{
		spdlog::info("Incoming: {}", std::string{data});
		conn->Send(data, len);
	}

	Common::IStreamSocketHandler* OnIncomingConnection() final
	{
		return this;
	}

private:
	EventLoop::EventLoop& mEv;
	EventLoop::EventLoop::Timer mTimer;

	Common::StreamSocket mSocket;
	Common::StreamSocketServer mServer;

	int mFd = 0;
	char mSockBuf[100];

};

#endif // EXAMPLE_H
