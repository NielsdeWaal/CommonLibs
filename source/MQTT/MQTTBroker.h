#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include "EventLoop.h"

namespace MQTTBroker {

enum MQTTPacketType
{
	CONNECT = 1,
	CONNACK = 2,
	PUBLISH = 3,
	PUBACK = 4,
	PUBREC = 5,
	PUBREL = 6,
	PUBCOMP = 7,
	SUBSCRIBE = 8,
	SUBACK = 9,
	UNSUBSCRIBE = 10,
	UNSUBACK = 11,
	PINGREQ = 12,
	PINGRESP = 13,
	DISCONNECT = 14,
};

class MQTTBroker : public Common::IStreamSocketHandler
				 , public Common::IStreamSocketServerHandler
{
public:
	MQTTBroker(EventLoop::EventLoop& ev)
		: mEv(ev)
		, mMQTTServer(mEv, this)
	{
		mLogger = spdlog::get("MQTTBroker");
		if(mLogger == nullptr)
		{
			auto exampleAppLogger = spdlog::stdout_color_mt("MQTTBroker");
			mLogger = spdlog::get("MQTTBroker");
		}
	}

	void OnConnected() final
	{
		mLogger->info("Connection to client established");
	}

	void OnDisconnect() final
	{
		mLogger->info("Connection with client terminated");
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
	EventLoop::EventLoop mEv;
	Common::StreamSocketServer mMQTTServer;
	std::vector<Common::StreamSocket> mClientConnections;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // MQTT_BROKER_H
