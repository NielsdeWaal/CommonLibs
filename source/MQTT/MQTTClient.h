#ifndef MQTTClIENT_H
#define MQTTCLIENT_H

#include "EventLoop.h"
#include "StreamSocket.h"

#include "MQTT/MQTTPacket.h"

using namespace std::chrono_literals;

namespace MQTT {

class MQTTClient;

class IMQTTClientHandler
{
public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect(MQTTClient* conn) = 0;
	//virtual void OnIncomingData(MQTTClient* conn, char* data, size_t len) = 0;
	virtual void OnPublish(const std::string& topic, const std::string& msg) = 0;
	virtual ~IMQTTClientHandler() {}
};

class MQTTClient : public Common::IStreamSocketHandler
{
public:
	MQTTClient(EventLoop::EventLoop& ev, IMQTTClientHandler* handler)
		: mEv(ev)
		, mConnection(mEv, this)
		, mHandler(handler)
		, mKeepAliveTimer(10s, EventLoop::EventLoop::TimerType::Repeating, [this](){ KeepAlive(); })
	{
		mLogger = spdlog::get("MQTTClient");
		if(mLogger == nullptr)
		{
			auto mqttclientlogger = spdlog::stdout_color_mt("MQTTClient");
			mLogger = spdlog::get("MQTTClient");
		}
	}

	~MQTTClient()
	{
		if(mMQTTConnected)
		{
			Disconnect();
		}
		if(mTCPConnected)
		{
			mConnection.Shutdown();
		}
	}

	const bool IsConnected() const noexcept
	{
		return (mTCPConnected && mMQTTConnected);
	}

	void Connect(const std::string& hostname, const uint16_t port)
	{
		mConnection.Connect(hostname.c_str(), port);
	}

	void Disconnect()
	{
		const MQTTDisconnectPacket disconPacket;
		const auto packet = disconPacket.GetMessage();

		mConnection.Send(packet.data(), packet.size());
		mMQTTConnected = false;
	}

	void Subscribe(const std::string& topic)
	{
		if(!mTCPConnected && !mMQTTConnected)
		{
			mLogger->error("Can't subscribe while not connected");
			return;
		}

		const MQTTSubscribePacket subPacket(mPacketIdentifier, topic);
		const auto packet = subPacket.GetMessage();

		mConnection.Send(packet.data(), packet.size());

		++mPacketIdentifier;
	}

	void UnSubscribe(const std::string& topic)
	{}

	void Publish(const std::string& topic, const std::string& message)
	{
		if(!mTCPConnected && !mMQTTConnected)
		{
			mLogger->error("Can't publish while not connected");
			return;
		}

		const MQTTPublishPacket pubPacket(mPacketIdentifier, topic, message);
		const auto packet = pubPacket.GetMessage();

		mConnection.Send(packet.data(), packet.size());

		//++mPacketIdentifier;
	}

	void OnConnected() final
	{
		mLogger->info("Connection succeeded");
		mTCPConnected = true;

		const MQTTConnectPacket connectPacket(60, "ClientID1", 1);
		const auto packet = connectPacket.GetMessage();
		mConnection.Send(packet.data(), packet.size());

		mEv.AddTimer(&mKeepAliveTimer);
	}

	void OnDisconnect(Common::StreamSocket* conn) final
	{
		mLogger->warn("Connection terminated");
	}

	void OnIncomingData(Common::StreamSocket* conn, char* data, size_t len) final
	{
		MQTTPacket incomingPacket(data);

		switch(incomingPacket.mFixedHeader.mType)
		{
			case MQTTPacketType::CONNACK:
			{
				mLogger->info("Incoming connack");
				mMQTTConnected = true;
				mHandler->OnConnected();
				break;
			}

			case MQTTPacketType::PUBLISH:
			{
				mHandler->OnPublish(incomingPacket.mPublish.mTopicFilter, incomingPacket.mPublish.mTopicPayload);
				break;
			}

			case MQTTPacketType::PINGRESP:
			{
				break;
			}

			case MQTTPacketType::SUBACK:
			{
				break;
			}

			default:
			{
				mLogger->warn("Unknown packet type");
				break;
			}

		}

	}

	void KeepAlive()
	{
		if(mTCPConnected && mMQTTConnected)
		{
			const MQTTPingRequestPacket packet;
			mConnection.Send(packet.GetMessage(), packet.GetSize());
		}
	}

private:
	EventLoop::EventLoop& mEv;
	Common::StreamSocket mConnection;
	IMQTTClientHandler* mHandler;

	EventLoop::EventLoop::Timer mKeepAliveTimer;

	bool mTCPConnected = false;
	bool mMQTTConnected = false;

	uint16_t mPacketIdentifier = 1;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // MQTTCLIENT_H
