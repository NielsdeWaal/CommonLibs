#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "EventLoop.h"

namespace Common {

using namespace EventLoop;

template<typename SocketType>
class WebsocketClient;

template<typename SocketType>
class IWebsocketClientHandler
{
private:
	static_assert(std::is_same<Common::StreamSocket, SocketType>::value, "SocketType is not compatible with websocket");
public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect(WebsocketClient<SocketType>* conn) = 0;
	virtual void OnIncomingData(WebsocketClient<SocketType>* conn, char* data, size_t len) = 0;
	virtual ~IWebsocketClientHandler() {}
};

template<typename SocketType>
class WebsocketClient : public Common::IStreamSocketHandler
{
private:
	static_assert(std::is_same<Common::StreamSocket, SocketType>::value, "SocketType is not compatible with websocket");

	enum class Opcode : std::uint8_t
	{
		CONTINUATION = 0,
		TEXT = 1,
		BINARY = 2,
		CLOSE = 8,
		PING = 9,
		PONG = 10,
	};

	struct WebsocketHeader {
		unsigned int mHeaderLength;
		bool mFin;
		bool mIsMasked;
		Opcode mOpcode;
		int mInitialLength;
		uint64_t mExtendedLength;
		uint8_t mMask[4];
	};

public:
	WebsocketClient(EventLoop::EventLoop& ev, IWebsocketClientHandler<SocketType>* handler) noexcept
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

	void OnDisconnect(SocketType* conn)
	{
		mLogger->warn("Connection to TCP endpoint closed");
	}

	void OnIncomingData(SocketType* conn, char* data, size_t len)
	{
		//FIXME Needs actual HTTP request handling instead of just looking at the first character
		if(data[0] == 'H')
		{
			mLogger->info("Connection with websocket server established");
			mHandler->OnConnected();
			mConnected = true;
			return;
		}

		WebsocketHeader header;
		header.mFin = (data[0] & 0x80) == 0x80;
		header.mOpcode = static_cast<Opcode>((data[0] & 0x0f));
		header.mIsMasked = (data[1] & 0x80) == 0x80;
		header.mInitialLength = (data[1] & 0x7f);
		header.mHeaderLength = 2 // Opcode and reserved bits
			+ (header.mInitialLength == 126? 2 : 0) // Standard length
			+ (header.mInitialLength == 127? 8 : 0) // Extended length
			+ (header.mIsMasked? 4 : 0); // Masking key

		size_t headerOffset = 0;

		if(!(header.mInitialLength < 126))
		{
			mLogger->warn("Incoming packet is too big for handling");
		}

		headerOffset = 2;

		if(header.mIsMasked)
		{
			header.mMask[0] = (static_cast<uint8_t>(data[headerOffset+0])) << 0;
			header.mMask[1] = (static_cast<uint8_t>(data[headerOffset+1])) << 0;
			header.mMask[2] = (static_cast<uint8_t>(data[headerOffset+2])) << 0;
			header.mMask[3] = (static_cast<uint8_t>(data[headerOffset+3])) << 0;
		}

		if(header.mOpcode == Opcode::TEXT
		|| header.mOpcode == Opcode::BINARY
		|| header.mOpcode == Opcode::CONTINUATION)
		{
			if(header.mFin)
			{
				mHandler->OnIncomingData(this, data+header.mHeaderLength, header.mInitialLength);
			}
		}
	}

	void Send(const char* data, const size_t len)
	{
		if(!mConnected)
		{
			mLogger->warn("Attempted to send while not connected");
			return;
		}
		const std::vector<char> maskingKey{0x12, 0x34, 0x56, 0x78};
		std::vector<char> header;
		header.assign(2 + (len >= 126 ? 2 : 0) + (len >= 65536 ? 6 : 0) , 0);

		std::string payload;
		const char* startPointer = data;
		const char* endPointer = startPointer + len;

		header[0] = 0x80 | static_cast<int>(Opcode::TEXT);

		header[1] = (len & 0xff) | (0x80);
		header.insert(std::begin(header)+2, std::begin(maskingKey), std::end(maskingKey));

		//FIXME
		//Ugly as sin but it should work
		int i = 0;
		while(startPointer != endPointer)
		{
			payload.push_back((*startPointer++) ^ maskingKey[i++ % 4]);
		}
		header.insert(std::begin(header)+2+maskingKey.size(), std::begin(payload), std::end(payload));

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
	IWebsocketClientHandler<SocketType>* mHandler;

	SocketType mSocket;

	uint16_t mPort;
	std::string mAddress;

	bool mConnected;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // WEBSOCKET_H
