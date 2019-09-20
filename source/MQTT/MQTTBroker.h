#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include <variant>

#include <spdlog/fmt/ostr.h>

#include "EventLoop.h"

namespace MQTTBroker {

//mLogger->info("	Protocol name: {}", std::string(data + 4, 4));
//mLogger->info("	Protocol level: {:#04x}", data[8]);
//if(data[8] == 0x04)
//{
//	mLogger->info("	Version = 3.1.1");
//}
//if(!(data[9] & (1)))
//{
//	mLogger->info("	Valid conenct flags");
//}
//mLogger->info("	Connect flags: {0:#010b}", data[9]);
//if(data[9] & (1 << 1))
//{
//	mLogger->info("	Connection request wants clean session");
//}
//mLogger->info("	Keep alive: {0:#04x} or {0:d} seconds", (data[10] << 8 | (data[11] & 0xFF)));
//mLogger->info("	Client-ID length: {}", (data[12] << 8 | (data[13] & 0xFF)));
//mLogger->info("	Incoming Client-id: {}", std::string(data + 14));

//mLogger->info("Incoming publish packet");
//mLogger->info("	Fixed header flags: {0:#010b}", static_cast<uint8_t>(data[0] & 0x00FF));
//mLogger->info("	Payload length: {:d} {:#04x}:{:#04x}", (data[2] << 8 | (data[3] & 0xFF)), data[2], data[3]);
//mLogger->info("	Topic name: {}", std::string(data + 4, (data[2] << 8 | (data[3] & 0xFF))));
//mLogger->info("	Topic payload: {}", std::string(data + (data[2] << 8 | (data[3] & 0xFF)) + 4, data[1] - (data[2] << 8 | (data[3] & 0xFF))));

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

//Helper for packet type visitor
//template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
//template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

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

class MQTTFixedHeader
{
public:
	MQTTFixedHeader(const char* data)
		: mType(static_cast<MQTTPacketType>(*data >> 4))
		, mRemainingLength(data[1])
	{}

	MQTTFixedHeader()
	{}

	MQTTPacketType mType;
private:
	//MQTTFlag mFlag;
	std::size_t mRemainingLength;
};

class MQTTConnectPacket
{
public:
	MQTTConnectPacket(const char* data)
		: mProtocolName(data, 4)
		, mProtocolLevel(data[4])
		, mConnectFlags(data[5])
		, mKeepAlive(data[6] << 8 | (data[7] & 0xFF))
		, mClientIDLength(data[8] << 8 | (data[9] & 0xFF))
		, mClientID(data + 10)
	{}

	MQTTConnectPacket()
	{}

	bool IsCleanSessionRequest() const
	{
		return (mConnectFlags & (1 << 1));
	}


private:
	std::string mProtocolName;
	std::uint8_t mProtocolLevel;
	std::uint8_t mConnectFlags;
	std::uint16_t mKeepAlive;
	std::size_t mClientIDLength;
	std::string mClientID;

	bool ValidateConnectFlags()
	{
		return (mConnectFlags & (1));
	}

	template<typename OStream>
	friend OStream &operator<<(OStream& os, const MQTTConnectPacket& packet)
	{
		return os << packet.mProtocolName;
	}
};

class MQTTPublishPacket
{
public:
	MQTTPublishPacket(const char* data, size_t len)
		: mTopicName(data, len)
		, mTopicPayload(data + len)
	{}

	MQTTPublishPacket()
	{}

//private:
	std::string mTopicName;
	std::string mTopicPayload;
};

class MQTTPacket
{
public:
	MQTTPacket(const char* data)
		: mFixedHeader(data)
	{
		//TODO Construct contents based on packet type in header
		if(mFixedHeader.mType == MQTTPacketType::CONNECT)
		{
			mConnect = MQTTConnectPacket(data + 4);
		}
		else if(mFixedHeader.mType == MQTTPacketType::PUBLISH)
		{
			mPublish = MQTTPublishPacket(data + 4, (data[2] << 8 | (data[3] & 0xFF)));
		}
	}

	MQTTFixedHeader mFixedHeader;
	//std::variant<MQTTConnectPacket> mContents;
	MQTTConnectPacket mConnect;
	MQTTPublishPacket mPublish;
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

private:
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
		MQTTPacket incomingPacket(data);

		switch(incomingPacket.mFixedHeader.mType)
		{
			case MQTTPacketType::CONNECT:
			{
				mLogger->info("Sending CONNACK");

				SendConnack(incomingPacket.mConnect, conn);

				mClientConnections.push_back(conn);
				break;
			}

			case MQTTPacketType::PUBLISH:
			{
				mLogger->info("Incoming publish:");
				mLogger->info("	topic: {}", incomingPacket.mPublish.mTopicName);
				mLogger->info("	payload: {}", incomingPacket.mPublish.mTopicPayload);
				break;
			}
		}

		//std::visit(overloaded {
		//		[](MQTTPacketType::CONNECT arg) {
		//		},
		//		[](MQTTPacketType::PUBLISH arg) {
		//		},
		//		[](auto arg) {
		//			mLogger->error("Unknown incoming packet");
		//		},
		//	}, incomingPacket.mFixedHeader.mType);

		//mLogger->info("Fixed incoming header: ");
		//mLogger->info("	Remaining length: {0:x}", data[1]);
		//if(static_cast<MQTTPacketType>(data[0] >> 4) == MQTTPacketType::CONNECT)
		//{
		//}
		//else if(static_cast<MQTTPacketType>(data[0] >> 4) == MQTTPacketType::PUBLISH)
		//{
		//}
		//else
		//{
		//	mLogger->info("Unknown type, dumping packet");
		//	mLogger->info("{}", std::string(data + 2, len - 2));
		//}
		//mLogger->info("Incoming: {}", std::string{data});
		//conn->Send(data, len);
		//mEv.SheduleForNextCycle([this](){OnNextCycle();});
	}

	void SendConnack(const MQTTConnectPacket& incConn, Common::StreamSocket* conn)
	{
		const char connack[] = {(static_cast<uint8_t>(MQTTPacketType::CONNACK) << 4), 2, 0, 0};
		//MQTT 3.2.2.2
		if(!incConn.IsCleanSessionRequest())
		{
			if(std::find(std::begin(mClientConnections),
										 std::end(mClientConnections), conn) != std::end(mClientConnections))
			{
				mLogger->warn("Existing client id found, but stored session is not yet supported");
			}
			else
			{
				mLogger->info("Existing client session not found, setting SP to 0");
			}
		}

		conn->Send(connack, sizeof(connack));
	}

	Common::IStreamSocketHandler* OnIncomingConnection() final
	{
		return this;
	}

	EventLoop::EventLoop& mEv;
	Common::StreamSocketServer mMQTTServer;
	std::vector<Common::StreamSocket*> mClientConnections;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // MQTT_BROKER_H
