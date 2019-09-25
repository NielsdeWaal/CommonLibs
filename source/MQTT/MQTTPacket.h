#ifndef MQTTPACKET_H
#define MQTTPACKET_H

namespace MQTT {

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

class MQTTSubackPacket
{
public:
	MQTTSubackPacket(std::uint16_t packetId, std::uint8_t retCode)
		: mMessage({
				static_cast<char>((static_cast<std::uint8_t>(MQTTPacketType::SUBACK) << 4 | 0))
				, 3
				, static_cast<char>(packetId >> 8)
				, static_cast<char>(packetId & 0xFF)
				, static_cast<char>(retCode)
				})
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
	std::array<char, 5> mMessage;

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

	//std::variant<MQTTConnectPacket> mContents;
	MQTTFixedHeader mFixedHeader;
	MQTTConnectPacket mConnect;
	MQTTPublishPacket mPublish;
	MQTTDisconnectPacket mDisconnect;
	MQTTSubscribePacket mSubscribe;
	MQTTPingRequestPacket mPingRequest;
	MQTTPingResponsePacket mPingResponse;
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
