#ifndef EXAMPLE_H
#define EXAMPLE_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "EventLoop.h"

#include "StreamSocket.h"
#include "UDPSocket.h"

#include "Statwriter/StatWriter.h"

using namespace std::chrono_literals;

class ExampleApp : public EventLoop::IEventLoopCallbackHandler
				 , public Common::IStreamSocketHandler
				 , public Common::IStreamSocketServerHandler
				 , public Common::IUDPSocketHandler
{
public:
	ExampleApp(EventLoop::EventLoop& ev)
		: mEv(ev)
		, mTimer(1s, EventLoop::EventLoop::TimerType::Repeating, [this](){ OnTimerCallback(); })
		, mSocket(mEv, this)
		, mServer(mEv, this)
		, mUDPClient(mEv, this)
		, mSW(mEv)
	{
		mLogger = spdlog::get("ExampleApp");
		if(mLogger == nullptr)
		{
			auto exampleAppLogger = spdlog::stdout_color_mt("ExampleApp");
			mLogger = spdlog::get("ExampleApp");
		}
		//mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);

		mSW.AddGroup("DEBUG", true);
		mSW.AddFieldToGroup("DEBUG", "Debug1", [this](){ mDebugMeasurementCounter++; return mDebugMeasurementCounter;});
		mSW.AddFieldToGroup("DEBUG", "Debug2", [this](){ mDebugMeasurementCounter1++; return mDebugMeasurementCounter1;});
		mSW.AddGroup("TestGroup", true);
		mSW.AddFieldToGroup("TestGroup", "Debug7", [this](){ mDebugMeasurementCounter++; return mDebugMeasurementCounter;});
	}

	~ExampleApp()
	{
		mSocket.Shutdown();
		mServer.Shutdown();
	}

	void Initialise()
	{
		//mEv.AddTimer(&mTimer);
		//mServer.BindAndListen(1337);
		//mSocket.Connect("127.0.0.1", 1337);
		mSW.InfluxConnector("127.0.0.1", 9555);
		mSW.SetBatchWriting(5s);
	}

	void OnTimerCallback()
	{
		//mLogger->info("Got callback from timer");
		//mSocket.Send(Teststring.c_str(), Teststring.size());
		//mUDPClient.Send(Teststring.c_str(), Teststring.size(), "127.0.0.1", 9999);
		mSW.DebugLineMessages();
	}

	void OnNextCycle()
	{
		mLogger->info("Callback from next cycle");
	}

	void OnEventLoopCallback() final
	{
		mLogger->info("Got callback from EV");
	}

	void OnConnected() final
	{
		mLogger->info("Connection succeeded");
	}

	void OnDisconnect() final
	{
		mLogger->warn("Connection terminated");
	}

	void OnIncomingData(Common::StreamSocket* conn, char* data, size_t len) final
	{
		mLogger->info("Incoming: {}", std::string{data});
		//conn->Send(data, len);
		mEv.SheduleForNextCycle([this](){OnNextCycle();});
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
	Common::UDPSocket mUDPClient;

	int mFd = 0;
	char mSockBuf[100];

	std::string Teststring{"Test string"};

	StatWriter::StatWriter mSW;
	int mDebugMeasurementCounter = 0;
	int mDebugMeasurementCounter1 = 8;

	std::shared_ptr<spdlog::logger> mLogger;

};

#endif // EXAMPLE_H
