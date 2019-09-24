#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include <deque>
#include <variant>

#include <spdlog/fmt/ostr.h>

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

enum MQTTQoSType
{
	ONE = 1,
	TWO = 2,
	THREE = 3,
};

class MQTTFixedHeader
{
public:
	MQTTFixedHeader(const char* data)
		: mType(static_cast<MQTTPacketType>(static_cast<uint8_t>(*data) >> 4))
		, mRemainingLength(data[1])
	{
		spdlog::get("MQTTBroker")->info("Fixed header type: {0:#010b}", (*data >> 4));
	}

	MQTTFixedHeader()
	{}

	std::size_t GetSize() const noexcept
	{
		return mRemainingLength;
	}

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

	bool IsCleanSessionRequest() const noexcept
	{
		return (mConnectFlags & (1 << 1));
	}

	std::string GetClientID() const noexcept
	{
		return mClientID;
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
	MQTTPublishPacket(const char* data, size_t topicLen, size_t payloadLen)
		: mTopicName(data, topicLen)
		, mTopicPayload(data + topicLen, payloadLen - 2)
	{}

	MQTTPublishPacket()
	{}

//private:
	std::string mTopicName;
	std::string mTopicPayload;
};

class MQTTDisconnectPacket
{
public:
	MQTTDisconnectPacket(const char* data)
	{
		if(data[0] != ((static_cast<uint8_t>(MQTTPacketType::DISCONNECT) << 4) & 0xF0))
		{
			mValidDisconnect = false;
		}
	}

	MQTTDisconnectPacket()
	{}

	bool IsPacketValid() const noexcept
	{
		return mValidDisconnect;
	}

private:
	bool mValidDisconnect = true;
};

//TODO Support multiple topics from a single sub packet
class MQTTSubscribePacket
{
public:
	MQTTSubscribePacket()
	{}

	MQTTSubscribePacket(const char* data)
		: mPacketIdentifier(data[0] << 8 | data[1])
		, mTopicLength(data[2] << 8 | data[3])
		, mTopicFilter(data + 4 , mTopicLength)
	{}

	std::uint16_t mPacketIdentifier;
	std::size_t mTopicLength;
	std::string mTopicFilter;

private:
	std::string mRest;
};

class MQTTPingRequestPacket
{
public:
	MQTTPingRequestPacket()
	{}
};

class MQTTPingResponsePacket
{
public:
	MQTTPingResponsePacket()
	{}
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
			mPublish = MQTTPublishPacket(data + 4, (data[2] << 8 | (data[3] & 0xFF)), mFixedHeader.GetSize() - (data[2] << 8 | (data[3] & 0xFF)));
		}
		else if(mFixedHeader.mType == MQTTPacketType::DISCONNECT)
		{
			mDisconnect = MQTTDisconnectPacket(data);
		}
		else if(mFixedHeader.mType == MQTTPacketType::SUBSCRIBE)
		{
			mSubscribe = MQTTSubscribePacket(data + 2);
		}
		else if(mFixedHeader.mType == MQTTPacketType::PINGREQ)
		{
			mPingRequest = MQTTPingRequestPacket();
		}
		else if(mFixedHeader.mType == MQTTPacketType::PINGRESP)
		{
			mPingResponse = MQTTPingResponsePacket();
		}
	}

	MQTTFixedHeader mFixedHeader;
	//std::variant<MQTTConnectPacket> mContents;
	MQTTConnectPacket mConnect;
	MQTTPublishPacket mPublish;
	MQTTDisconnectPacket mDisconnect;
	MQTTSubscribePacket mSubscribe;
	MQTTPingRequestPacket mPingRequest;
	MQTTPingResponsePacket mPingResponse;
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

	void OnDisconnect(Common::StreamSocket* conn) final
	{
		mLogger->info("Connection with client terminated");
		for(auto& client : mPubClients)
		{
			for(auto it = client.second.begin(); it != client.second.end(); ) {
				if(*it == conn)
					it = client.second.erase(it);
				else
					++it;
			}
		}
	}

	void OnIncomingData(Common::StreamSocket* conn, char* data, size_t len) final
	{
		MQTTPacket incomingPacket(data);

		mLogger->info("Incoming packet");

		switch(incomingPacket.mFixedHeader.mType)
		{
			case MQTTPacketType::CONNECT:
			{
				mLogger->info("Incoming connect");
				mLogger->info("	Client identifier: {}", incomingPacket.mConnect.GetClientID());

				mLogger->info("	Sending CONNACK");
				SendConnack(incomingPacket.mConnect, conn);

				mClientConnections.insert({incomingPacket.mConnect.GetClientID(), conn});
				break;
			}

			case MQTTPacketType::PUBLISH:
			{
				mLogger->info("Incoming publish:");
				mLogger->info("	topic: {}", incomingPacket.mPublish.mTopicName);
				mLogger->info("	payload: {}", incomingPacket.mPublish.mTopicPayload);

				//mPubQueue[incomingPacket.mPublish.mTopicName].push_back(incomingPacket.mPublish.mTopicPayload);

				const auto& clients = mPubClients[incomingPacket.mPublish.mTopicName];

				for(const auto& client : clients)
				{
					client->Send(data, len);
				}

				break;
			}

			case MQTTPacketType::DISCONNECT:
			{
				mLogger->info("Incoming disconnect");
				mLogger->info("	Removing client");

				for(auto it = mClientConnections.begin(); it != mClientConnections.end(); ) {
					if(it->second == conn)
						it = mClientConnections.erase(it);
					else
						++it;
				}

				break;
			}

			case MQTTPacketType::SUBSCRIBE:
			{
				mLogger->info("Incoming Subscribe:");
				mLogger->info("	Packet identifier: {}", incomingPacket.mSubscribe.mPacketIdentifier);
				mLogger->info("	Topic length: {}", incomingPacket.mSubscribe.mTopicLength);
				mLogger->info("	Topic filter: {}", incomingPacket.mSubscribe.mTopicFilter);

				mPubClients[incomingPacket.mSubscribe.mTopicFilter].push_back(conn);

				SendSuback(incomingPacket.mSubscribe, conn);

				break;
			}

			case MQTTPacketType::PINGREQ:
			{
				mLogger->info("Incoming ping request, sending response");

				SendPingResponse(conn);
				break;
			}

			default:
			{
				mLogger->warn("Unhandled packet type!!!");
				mLogger->info("	Type: {0:#010b}", (data[0] >> 4));
				break;
			}
		}
	}

	void SendConnack(const MQTTConnectPacket& incConn, Common::StreamSocket* conn)
	{
		const char connack[] = {(static_cast<uint8_t>(MQTTPacketType::CONNACK) << 4), 2, 0, 0};
		//MQTT 3.2.2.2
		if(!incConn.IsCleanSessionRequest())
		{
			const auto clientID = mClientConnections.find(incConn.GetClientID());
			if(clientID != mClientConnections.end())
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

	void SendSuback(const MQTTSubscribePacket& subPacket, Common::StreamSocket* conn)
	{
		const uint8_t suback[] = {(static_cast<uint8_t>(MQTTPacketType::SUBACK) << 4),
			3,
			static_cast<uint8_t>((subPacket.mPacketIdentifier >> 8)),
			static_cast<uint8_t>((subPacket.mPacketIdentifier & 0xFF)),
			0};
		mLogger->info("Sending suback: {:#04x} {:#04x} {:#04x} {:#04x} {:#04x}", suback[0], suback[1], suback[2], suback[3], suback[4]);
		mLogger->info("Sending suback: {:#010b} {:#010b} {:#010b} {:#010b} {:#010b}", suback[0], suback[1], suback[2], suback[3], suback[4]);
		conn->Send(suback, sizeof(suback));
	}

	void SendPingResponse(Common::StreamSocket* conn)
	{
		const uint8_t pingResp[] = {static_cast<uint8_t>(MQTTPacketType::PINGRESP) << 4, 0};
		conn->Send(pingResp, sizeof(pingResp));
	}

	Common::IStreamSocketHandler* OnIncomingConnection() final
	{
		return this;
	}

	EventLoop::EventLoop& mEv;
	Common::StreamSocketServer mMQTTServer;
	//std::vector<Common::StreamSocket*> mClientConnections;
	std::unordered_map<std::string, Common::StreamSocket*> mClientConnections;
	std::unordered_map<std::string, std::vector<Common::StreamSocket*>> mPubClients;
	//[TOPIC]->QUEUEU<PAYLOAD>
	//This means that there is no wildcard support yet.
	std::unordered_map<std::string, std::deque<std::string>> mPubQueue;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // MQTT_BROKER_H
