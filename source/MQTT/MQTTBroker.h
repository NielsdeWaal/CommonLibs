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

// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718022
//enum MQTTFlag
//{
//	CONNECT = 0,
//	CONNACK = 0,
//	PUBLISH = 0,
//	PUBACK = 0,
//	PUBREC = 0,
//	PUBREL = b'0010,
//	PUBCOMP = 7,
//	SUBSCRIBE = 8,
//	SUBACK = 9,
//	UNSUBSCRIBE = 10,
//	UNSUBACK = 11,
//	PINGREQ = 12,
//	PINGRESP = 13,
//	DISCONNECT = 14,
//};

class MQTTPacket
{
	MQTTPacketType mType;
	//MQTTFlag mFlag;
	std::size_t mRemainingLength;
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

	void Initialise()
	{
		mMQTTServer.BindAndListen(1883);
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
		mLogger->info("Fixed incoming header: ");
		mLogger->info("	Remaining length: {0:x}", data[1]);
		if(static_cast<MQTTPacketType>(data[0] >> 4) == MQTTPacketType::CONNECT)
		{
			mLogger->info("Incoming connect packet");
			mLogger->info("	Protocol name: {}", std::string(data + 4, 4));
			mLogger->info("	Protocol level: {:#04x}", data[8]);
			if(data[8] == 0x04)
			{
				mLogger->info("	Version = 3.1.1");
			}
			if(!(data[9] & (1)))
			{
				mLogger->info("	Valid conenct flags");
			}
			mLogger->info("	Connect flags: {0:#010b}", data[9]);
			if(data[9] & (1 << 1))
			{
				mLogger->info("	Connection request wants clean session");
			}
			mLogger->info("	Keep alive: {0:#04x} or {0:d} seconds", (data[10] << 8 | (data[11] & 0xFF)));
			mLogger->info("	Client-ID length: {}", (data[12] << 8 | (data[13] & 0xFF)));
			mLogger->info("Incoming Client-id: {}", std::string(data + 14));
		}
		//mLogger->info("Incoming: {}", std::string{data});
		//conn->Send(data, len);
		//mEv.SheduleForNextCycle([this](){OnNextCycle();});
	}

	Common::IStreamSocketHandler* OnIncomingConnection() final
	{
		return this;
	}

private:
	EventLoop::EventLoop& mEv;
	Common::StreamSocketServer mMQTTServer;
	//std::vector<Common::StreamSocket> mClientConnections;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // MQTT_BROKER_H
