#ifndef MQTTPACKET_H
#define MQTTPACKET_H

#include <variant>

namespace MQTT {

enum class MQTTPacketType : char
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

enum class MQTTQoSType : char
{
	ONE = 1,
	TWO = 2,
	THREE = 3,
};

class MQTTHeaderOnlyPacket
{
public:
	MQTTHeaderOnlyPacket(MQTTPacketType type, std::uint8_t flags)
		: mMessage({static_cast<char>((static_cast<std::uint8_t>(type) << 4 | (flags & 0x0F))), 0})
	{}

	const char* GetMessage() const
	{
		return mMessage.data();
	}

	constexpr std::size_t GetSize() const
	{
		return mMessage.size();
	}

private:
	std::array<char, 2> mMessage;
};

class MQTTHeaderIdPacket
{
public:
	MQTTHeaderIdPacket(MQTTPacketType type, std::uint8_t flags, std::uint16_t id)
		: mMessage({static_cast<char>((static_cast<std::uint8_t>(type) << 4 | (flags & 0x0F))),
				0,
				static_cast<char>(id >> 8),
				static_cast<char>(id & 0xFF)})
	{}

	const char* GetMessage() const
	{
		return mMessage.data();
	}

	constexpr std::size_t GetSize() const
	{
		return mMessage.size();
	}

private:
	std::array<char, 4> mMessage;
};

class MQTTFixedHeader
{
public:
	MQTTFixedHeader(const char* data)
		: mType(static_cast<MQTTPacketType>(static_cast<uint8_t>(*data) >> 4))
		, mRemainingLength(data[1])
	{}

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

	MQTTConnectPacket(const std::uint16_t keepAlive,
					const std::string clientId,
					bool cleanSession)
		: mKeepAlive(keepAlive)
		, mClientID(clientId)
		, mCleanSession(cleanSession)
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

	std::vector<char> GetMessage() const noexcept
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::CONNECT) << 4);
		message.push_back(static_cast<char>(12 + mClientID.size()));

		message.insert(std::end(message), std::begin(mProtocolNameAndLevel), std::end(mProtocolNameAndLevel));

		//TODO Implement flags for e.g will
		message.push_back(2);

		message.push_back(static_cast<char>(mKeepAlive >> 8));
		message.push_back(static_cast<char>(mKeepAlive & 0x0F));

		message.push_back(static_cast<char>(mClientID.size() >> 8));
		message.push_back(static_cast<char>(mClientID.size() & 0x0F));
		message.insert(std::end(message), std::begin(mClientID), std::end(mClientID));

		return message;
	}

private:
	std::string mProtocolName;
	std::uint8_t mProtocolLevel;
	std::uint8_t mConnectFlags;
	std::uint16_t mKeepAlive;
	std::size_t mClientIDLength;
	std::string mClientID;
	bool mCleanSession;

	std::vector<char> mProtocolNameAndLevel{0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04};

	bool ValidateConnectFlags()
	{
		return (mConnectFlags & (1));
	}
};

class MQTTPublishPacket
{
public:
	MQTTPublishPacket()
	{}

	MQTTPublishPacket(const char* data, size_t topicLen, size_t payloadLen)
		: mTopicFilter(data, topicLen)
		, mTopicPayload(data + topicLen, payloadLen - 2)
	{}

	MQTTPublishPacket(std::uint16_t packetID, const std::string& topic,
			const std::string& msg, std::optional<int> qos)
		: mTopicFilter(topic)
		, mTopicPayload(msg)
		, mPacketIdentifier(packetID)
	{
		mQoS = qos.value_or(0);
	}

	std::vector<char> GetMessage() const noexcept
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::PUBLISH) << 4);

		message.push_back(static_cast<char>(2 + // var header
											mTopicFilter.size() + // size bytes + topic size
											mTopicPayload.size() // QoS byte
											));

		message.push_back(static_cast<char>(mTopicFilter.size() >> 8));
		message.push_back(static_cast<char>(mTopicFilter.size() & 0x0F));

		message.insert(std::end(message), std::begin(mTopicFilter), std::end(mTopicFilter));

		if(mQoS)
		{
			message.push_back(static_cast<char>(mPacketIdentifier >> 8));
			message.push_back(static_cast<char>(mPacketIdentifier & 0x0F));
		}

		message.insert(std::end(message), std::begin(mTopicPayload), std::end(mTopicPayload));

		message.push_back(0);

		return message;
	}

	std::string GetTopicFilter() const noexcept
	{
		return mTopicFilter;
	}

	std::string GetTopicPayload() const noexcept
	{
		return mTopicPayload;
	}

private:
	std::string mTopicFilter;
	std::string mTopicPayload;
	std::uint16_t mPacketIdentifier;

	int mQoS = 0;
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

	std::vector<char> GetMessage() const noexcept
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::DISCONNECT) << 4);
		message.push_back(0);

		return message;
	}

	bool IsPacketValid() const noexcept
	{
		return mValidDisconnect;
	}

private:
	bool mValidDisconnect = true;
};

class MQTTPingRequestPacket
{
public:
	MQTTPingRequestPacket()
		: mPacket(MQTTPacketType::PINGREQ, 0)
	{}

	const char* GetMessage() const
	{
		return mPacket.GetMessage();
	}

	std::size_t GetSize() const
	{
		return mPacket.GetSize();
	}

private:
	MQTTHeaderOnlyPacket mPacket;
};

class MQTTPingResponsePacket
{
public:
	MQTTPingResponsePacket()
		: mPacket(MQTTPacketType::PINGRESP, 0)
	{}

	const char* GetMessage() const
	{
		return mPacket.GetMessage();
	}

	std::size_t GetSize() const
	{
		return mPacket.GetSize();
	}

private:
	MQTTHeaderOnlyPacket mPacket;
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

	MQTTSubscribePacket(std::uint16_t packetId, const std::string& topic)
		: mPacketIdentifier(packetId)
		, mTopicLength(topic.size())
		, mTopicFilter(topic)
	{}

	std::vector<char> GetMessage() const noexcept
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::SUBSCRIBE) << 4 | 0b0000010);
		message.push_back(static_cast<char>(2 + // var header
											2 + mTopicLength + // size bytes + topic size
											1 // QoS byte
											));

		message.push_back(static_cast<char>(mPacketIdentifier >> 8));
		message.push_back(static_cast<char>(mPacketIdentifier & 0x0F));

		message.push_back(static_cast<char>(mTopicLength >> 8));
		message.push_back(static_cast<char>(mTopicLength & 0x0F));

		message.insert(std::end(message), std::begin(mTopicFilter), std::end(mTopicFilter));

		message.push_back(0); // QoS

		return message;
	}

private:
	std::uint16_t mPacketIdentifier;
	std::size_t mTopicLength;
	std::string mTopicFilter;
};

class MQTTUnsubscribePacket
{
public:
	MQTTUnsubscribePacket()
	{}

	MQTTUnsubscribePacket(const char* data)
		: mPacketIdentifier(data[0] << 8 | data[1])
		, mTopicLength(data[2] << 8 | data[3])
		, mTopicFilter(data + 4 , mTopicLength)
	{}

	MQTTUnsubscribePacket(std::uint16_t packetId, const std::string& topic)
		: mPacketIdentifier(packetId)
		, mTopicLength(topic.size())
		, mTopicFilter(topic)
	{}

	std::vector<char> GetMessage() const noexcept
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::UNSUBSCRIBE) << 4 | 0b0010);
		message.push_back(static_cast<char>(2 + // var header
											2 + mTopicLength // size bytes + topic size
											));

		message.push_back(static_cast<char>(mPacketIdentifier >> 8));
		message.push_back(static_cast<char>(mPacketIdentifier & 0x0F));

		message.push_back(static_cast<char>(mTopicLength >> 8));
		message.push_back(static_cast<char>(mTopicLength & 0x0F));

		message.insert(std::end(message), std::begin(mTopicFilter), std::end(mTopicFilter));

		return message;
	}

private:
	std::uint16_t mPacketIdentifier;
	std::size_t mTopicLength;
	std::string mTopicFilter;
};

//TODO return code needs to be in typed enum so validity can be checked
class MQTTSubackPacket
{
public:
	MQTTSubackPacket()
	{}

	MQTTSubackPacket(std::uint16_t packetId, std::uint8_t retCode)
		: mPacketIdentifier(packetId)
		, mReturnCode(retCode)
	{}

	MQTTSubackPacket(const char* data)
		: mPacketIdentifier(data[0] << 8 | data[1])
		, mReturnCode(data[3])
	{}

	std::vector<char> GetMessage() const
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::SUBACK) << 4 | 0);
		message.push_back(3); // packet length

		message.push_back(static_cast<char>(mPacketIdentifier >> 8));
		message.push_back(static_cast<char>(mPacketIdentifier & 0x0F));

		message.push_back(mReturnCode);

		return message;
	}

	std::uint16_t GetPacketId() const noexcept
	{
		return mPacketIdentifier;
	}

private:
	std::uint16_t mPacketIdentifier;
	std::uint8_t mReturnCode;
};

class MQTTUnsubackPacket
{
public:
	MQTTUnsubackPacket()
	{}

	MQTTUnsubackPacket(std::uint16_t packetId)
		: mPacketIdentifier(packetId)
	{}

	MQTTUnsubackPacket(const char* data)
		: mPacketIdentifier(data[0] << 8 | data[1])
	{}

	std::vector<char> GetMessage() const
	{
		std::vector<char> message;
		message.push_back(static_cast<char>(MQTTPacketType::UNSUBACK) << 4 | 0);
		message.push_back(2); // packet length

		message.push_back(static_cast<char>(mPacketIdentifier >> 8));
		message.push_back(static_cast<char>(mPacketIdentifier & 0x0F));

		return message;
	}

	std::uint16_t GetPacketId() const noexcept
	{
		return mPacketIdentifier;
	}

private:
	std::uint16_t mPacketIdentifier;
};

class MQTTPacket
{
public:
	MQTTPacket(const char* data)
		: mFixedHeader(data)
	{
		switch(mFixedHeader.mType)
		{
			case MQTTPacketType::CONNECT:
			{
				mContents = MQTTConnectPacket(data + 4);
				break;
			}
			case MQTTPacketType::PUBLISH:
			{
				mContents = MQTTPublishPacket(data + 4, (data[2] << 8 | (data[3] & 0xFF)), mFixedHeader.GetSize() - (data[2] << 8 | (data[3] & 0xFF)));
				break;
			}
			case MQTTPacketType::DISCONNECT:
			{
				mContents = MQTTDisconnectPacket(data);
				break;
			}
			case MQTTPacketType::SUBSCRIBE:
			{
				mContents = MQTTSubscribePacket(data + 2);
				break;
			}
			case MQTTPacketType::SUBACK:
			{
				mContents = MQTTSubackPacket(data + 2);
				break;
			}
			case MQTTPacketType::UNSUBACK:
			{
				mContents = MQTTUnsubackPacket(data + 2);
				break;
			}
			case MQTTPacketType::PINGREQ:
			{
				mContents = MQTTPingRequestPacket();
				break;
			}
			case MQTTPacketType::PINGRESP:
			{
				mContents = MQTTPingResponsePacket();
				break;
			}
		}
	}

	auto GetPublishPacket() const noexcept
	{
		return std::get_if<MQTTPublishPacket>(&mContents);
	}

	auto GetSubAckPacket() const noexcept
	{
		return std::get_if<MQTTSubackPacket>(&mContents);
	}

	auto GetUnSubAckPacket() const noexcept
	{
		return std::get_if<MQTTUnsubackPacket>(&mContents);
	}

	std::variant<MQTTConnectPacket
				,MQTTPublishPacket
				,MQTTDisconnectPacket
				,MQTTSubscribePacket
				,MQTTSubackPacket
				,MQTTUnsubackPacket
				,MQTTPingRequestPacket
				,MQTTPingResponsePacket> mContents;
	MQTTFixedHeader mFixedHeader;
};

class MQTTConnackPacket
{
public:
	MQTTConnackPacket(MQTTConnectPacket incConn, bool sessionPresent)
		: mMessage({
				static_cast<char>((static_cast<std::uint8_t>(MQTTPacketType::CONNACK) << 4 | 0 )),
				0b0010,
				static_cast<char>(sessionPresent ? 1 : 0),
				//static_cast<char>(return_code)
				0
				})
	{}

	const char* GetMessage() const
	{
		return mMessage.data();
	}

	std::size_t GetSize() const
	{
		return mMessage.size();
	}

private:
	std::array<char, 4> mMessage;
};

}

#endif // MQTTPACKET_H
