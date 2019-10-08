#ifndef EXAMPLE_H
#define EXAMPLE_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "EventLoop.h"

#include "MQTT/MQTTClient.h"
#include "StreamSocket.h"
#include "UDPSocket.h"

#include "Statwriter/StatWriter.h"

using namespace std::chrono_literals;

class ExampleApp : public EventLoop::IEventLoopCallbackHandler
				 //, public Common::IStreamSocketHandler
				 //, public Common::IStreamSocketServerHandler
				 //, public Common::IUDPSocketHandler
				 , public MQTT::IMQTTClientHandler
{
public:
	ExampleApp(EventLoop::EventLoop& ev)
		: mEv(ev)
		, mTimer(1s, EventLoop::EventLoop::TimerType::Repeating, [this](){ OnTimerCallback(); })
		//, mSocket(mEv, this)
		//, mServer(mEv, this)
		//, mUDPClient(mEv, this)
		, mMQTTClient(mEv, this)
		, mSW(mEv)
	{
		mLogger = spdlog::get("ExampleApp");
		if(mLogger == nullptr)
		{
			auto exampleAppLogger = spdlog::stdout_color_mt("ExampleApp");
			mLogger = spdlog::get("ExampleApp");
		}
		//mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);

		mMQTTClient.Initialise("Client1", 60);

		mSW.AddGroup("DEBUG", true);
		mSW.AddFieldToGroup("DEBUG", "Debug1", [this]() -> float { mDebugMeasurementCounter++; return mDebugMeasurementCounter;});
		mSW.AddFieldToGroup("DEBUG", "Debug2", [this]() -> int { mDebugMeasurementCounter1++; return mDebugMeasurementCounter1;});
		//mSW.AddGroup("TestGroup", true);
		//mSW.AddFieldToGroup("TestGroup", "Debug7", [this](){ mDebugMeasurementCounter++; return mDebugMeasurementCounter;});
	}

	~ExampleApp()
	{
		//mSocket.Shutdown();
		//mServer.Shutdown();
	}

	void Initialise()
	{
		mEv.AddTimer(&mTimer);
		//mServer.BindAndListen(1337);
		//mSocket.Connect("127.0.0.1", 1337);
		mMQTTClient.Connect("127.0.0.1", 1883);
		mSW.InfluxConnector("127.0.0.1", 9555);
		mSW.SetBatchWriting(5s);
	}

	void OnTimerCallback()
	{
		//mLogger->info("Got callback from timer");
		//mSocket.Send(Teststring.c_str(), Teststring.size());
		//mUDPClient.Send(Teststring.c_str(), Teststring.size(), "127.0.0.1", 9999);
		if(mMQTTClient.IsConnected())
		{
			mMQTTClient.Publish("test/TestTopic", "TestMessageFromCommonLibs");
		}
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
		mMQTTClient.Subscribe("test/TestTopic");
		mMQTTClient.Subscribe("SCD30");
	}

	void OnDisconnect(MQTT::MQTTClient* conn) final
	{
		mLogger->warn("Connection terminated");
	}

	//void OnIncomingData(Common::StreamSocket* conn, char* data, size_t len) final
	//{
	//	mLogger->info("Incoming: {}", std::string{data});
	//	//conn->Send(data, len);
	//	//mEv.SheduleForNextCycle([this](){OnNextCycle();});
	//}

	//Common::IStreamSocketHandler* OnIncomingConnection() final
	//{
	//	return this;
	//}

	void OnPublish(const std::string& topic, const std::string& msg) override
	{
		mLogger->info("Incoming publish, topic: {}, msg: {}", topic, msg);
		mMQTTClient.Unsubscribe("test/TestTopic");
	}

private:
	EventLoop::EventLoop& mEv;
	EventLoop::EventLoop::Timer mTimer;

	//Common::StreamSocket mSocket;
	//Common::StreamSocketServer mServer;
	//Common::UDPSocket mUDPClient;
	MQTT::MQTTClient mMQTTClient;

	int mFd = 0;
	char mSockBuf[100];

	std::string Teststring{"Test string"};

	StatWriter::StatWriter mSW;
	float mDebugMeasurementCounter = 0;
	int mDebugMeasurementCounter1 = 8;

	std::shared_ptr<spdlog::logger> mLogger;

};

#endif // EXAMPLE_H
