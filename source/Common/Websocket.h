#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "EventLoop.h"

namespace Common {

using namespace EventLoop;

class WebsocketClient;

class IWebsocketClientHandler
{
public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect(WebsocketClient* conn) = 0;
	virtual void OnIncomingData(WebsocketClient* conn, char* data, size_t len) = 0;
	virtual ~IWebsocketClientHandler() {}
};

class WebsocketClient : public Common::IStreamSocketHandler
{
private:
	enum class Opcode : std::uint8_t
	{
		CONTINUATION = 0,
		TEXT = 1,
		BINARY = 2,
		CLOSE = 8,
		PING = 9,
		PONG = 10,
	};

/*
	struct WebsocketPacket
	{

	};
*/

public:
	WebsocketClient(EventLoop::EventLoop& ev, IWebsocketClientHandler* handler) noexcept
		: mEventLoop(ev)
		, mHandler(handler)
		, mSocket(ev, this)
	{
		mLogger = mEventLoop.RegisterLogger("WebsocketClient");
	}

	void Connect(const char* addr, const uint16_t port)
	{
		mSocket.Connect(addr, port);
		mPort = port;
		mAddress = std::string{addr};
	}

	void OnConnected()
	{
		mLogger->debug("Connected to TCP endpoint");
		StartWebsocketConnection();
	}

	void OnDisconnect(StreamSocket* conn)
	{
		mLogger->warn("Connection to TCP endpoint closed");
	}

	void OnIncomingData(StreamSocket* conn, char* data, size_t len)
	{}

	void Send(const char* data, const size_t len)
	{
		std::vector<char> header;
		std::string payload(data, len);
		header.push_back(0x80 | static_cast<int>(Opcode::TEXT));

		header.push_back((len & 0xff) | (0x0));

		header.insert(std::end(header), std::begin(payload), std::end(payload));

		mSocket.Send(header.data(), header.size());
	}

	void StartWebsocketConnection()
	{
		//std::string connectString = fmt::format(" GET / HTTP/1.1\r\n Host: {0}:{1}\r\n Connection: Upgrade\r\n Upgrade: websocket\r\n Sec-Websocket-Version: 13\r\n Sec-Websocket-Key: x3JJHMbDL1EzLkh9GBhXDw\r\n \r\n ", mAddress, mPort);

		std::stringstream ss;
		ss << "GET / HTTP/1.1\r\n"
		<< "Host: " << mAddress << ":" << mPort << "\r\n"
		<< "Connection: Upgrade\r\n"
		<< "Upgrade: websocket\r\n"
		<< "Sec-Websocket-Version: 13\r\n"
		<< "Sec-Websocket-Key: x3JJHMbDL1EzLkh9GBhXDw\r\n"
		<< "\r\n";
		std::string connectString = ss.str();

		mSocket.Send(connectString.c_str(), connectString.size());
	}

private:
	EventLoop::EventLoop& mEventLoop;
	IWebsocketClientHandler* mHandler;

	Common::StreamSocket mSocket;

	uint16_t mPort;
	std::string mAddress;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // WEBSOCKET_H
