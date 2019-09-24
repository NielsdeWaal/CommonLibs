#ifndef MQTTClIENT_H
#define MQTTCLIENT_H

#include "EventLoop.h"

namespace MQTT {

class IMQTTClientHandler
{
	virtual void OnConnected() = 0;
	virtual void OnDisconnect(MQTTClient* conn) = 0;
	//virtual void OnIncomingData(MQTTClient* conn, char* data, size_t len) = 0;
	virtual void OnPublish(const std::string& topic, const std::string& msg) = 0;
	virtual ~IStreamSocketHandler() {}
};

class MQTTClient
{
public:
	MQTTClient(EventLoop::EventLoop& ev)
		: mEv(ev)
	{
		mLogger = spdlog::get("MQTTClient");
		if(mLogger == nullptr)
		{
			auto mqttclientlogger = spdlog::stdout_color_mt("MQTTClient");
			mLogger = spdlog::get("MQTTClient");
		}
	}

	void Connect(std::string hostname, const uint16_t port)
	{}

	void Disconnect()
	{}

	void Subscribe(const std::string& topic)
	{}

	void Publish(const std::string& topic, const std::string& message)
	{}

private:
	EventLoop::EventLoop& mEv;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // MQTTCLIENT_H
