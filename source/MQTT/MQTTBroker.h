#ifndef MQTT_BROKER_H
#define MQTT_BROKER_H

#include <deque>
#include <variant>

#include <spdlog/fmt/ostr.h>

#include "EventLoop.h"

#include "MQTTPacket.h"

namespace MQTTBroker {

using namespace MQTT;

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
			auto mqttclientlogger = spdlog::stdout_color_mt("MQTTBroker");
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
		const auto connack = MQTTConnackPacket(incConn, incConn.IsCleanSessionRequest());

		conn->Send(connack.GetMessage(), connack.GetSize());
	}

	void SendSuback(const MQTTSubscribePacket& subPacket, Common::StreamSocket* conn)
	{
		const auto suback = MQTTSubackPacket(subPacket.mPacketIdentifier, 0);
		conn->Send(suback.GetMessage(), suback.GetSize());
	}

	void SendPingResponse(Common::StreamSocket* conn)
	{
		auto pingResp = MQTTPingResponsePacket();
		conn->Send(pingResp.GetMessage(), pingResp.GetSize());
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
