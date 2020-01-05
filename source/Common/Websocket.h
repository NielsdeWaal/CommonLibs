#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "EventLoop.h"

namespace Common {

using namespace EventLoop;

template<typename SocketType, typename Handler>
class WebsocketClient;

template<typename SocketType, typename Handler>
class IWebsocketClientHandler
{
private:
	static_assert(std::is_same_v<Common::StreamSocket, SocketType> || std::is_same_v<Common::TLSSocket, SocketType>, "SocketType is not compatible with websocket");
public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect(WebsocketClient<SocketType, Handler>* conn) = 0;
	virtual void OnIncomingData(WebsocketClient<SocketType, Handler>* conn, const char* data, const size_t len) = 0;
	virtual ~IWebsocketClientHandler() {}
};

/**
 * @brief simple class for setting up client websocket connection
 *
 * TODO We need a more clean way for specifing the port, maybe default to either 80 of 443 depending on the SocketType, and if specified use another port.
 * TODO Better connect string solution. Now we just construct one roughly without any thought.
 */

template<typename SocketType, typename Handler>
class WebsocketClient : public Handler
{
private:
	static_assert(std::is_same_v<Common::StreamSocket, SocketType> || std::is_same_v<Common::TLSSocket, SocketType>, "SocketType is not compatible with websocket");

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
		uint16_t mExtendedLength16;
		uint8_t mMask[4];
	};

public:
	WebsocketClient(EventLoop::EventLoop& ev, IWebsocketClientHandler<SocketType, Handler>* handler) noexcept : mEventLoop(ev)
		, mHandler(handler)
		, mSocket(ev, this)
	{
		mLogger = mEventLoop.RegisterLogger("WebsocketClient");
	}

	/**
	 * @brief Send connection request to websocket server.
	 *
	 * Starts TCP connection to websocket server.
	 * When connection this class will send a connection request string
	 * in order to connect with the websocket endpoint.
	 */
	void Connect(const char* addr, const std::string& additionalURL, const uint16_t port = 0)
	{
		mSocket.Connect(addr, port);
		mPort = port;
		mAddress = std::string{addr};
		mUrl = additionalURL;
	}

	void ConnectHostname(const std::string& hostname, const std::uint16_t port, const std::string& additionalURL)
	{
		mSocket.ConnectHostname(hostname, port);
		mPort = port;
		mAddress = hostname;
		mUrl = additionalURL;
	}

	void Send(const char* data, const size_t len)
	{
		if(!mConnected)
		{
			mLogger->warn("Attempted to send while not connected");
			return;
		}
		const std::vector<char> maskingKey{0x12, 0x34, 0x56, 0x78}; // Do we even care?
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

private:
	void OnConnected()
	{
		mLogger->info("Connected to TCP endpoint");
		StartWebsocketConnection();
	}

	void OnDisconnect(SocketType* conn)
	{
		mLogger->warn("Connection to TCP endpoint closed");
	}

	void OnDisconnect(StreamSocket* conn)
	{
		mLogger->warn("Websocket testing server disconnected");
	}

	void OnIncomingData(SocketType* conn, char* data, size_t len)
	{
		//TODO Needs actual HTTP request handling instead of just looking at the first character
		if(data[0] == 'H' && !mConnected)
		{
			mLogger->info("Connection with websocket server established");
			mLogger->info("Incoming HTTP: {}", std::string{data});
			mConnected = true;
			mHandler->OnConnected();
			return;
		}

		const uint8_t* dataPtr = (uint8_t*)data;

		WebsocketHeader header;
		header.mFin = (dataPtr[0] & 0x80) == 0x80;
		header.mOpcode = static_cast<Opcode>((dataPtr[0] & 0x0F));
		header.mIsMasked = (dataPtr[1] & 0x80) == 0x80;
		header.mInitialLength = (dataPtr[1] & 0x7F);
		header.mHeaderLength = 2 // Opcode and reserved bits
			+ (header.mInitialLength == 126 ? 2 : 0) // Standard length
			+ (header.mInitialLength == 127 ? 8 : 0) // Extended length
			+ (header.mIsMasked? 4 : 0); // Masking key

		size_t headerOffset = 0;

		if(header.mInitialLength < 126)
		{
			header.mExtendedLength = 0;
			header.mExtendedLength = header.mInitialLength;
			headerOffset = 2;
		}
		else if(header.mInitialLength == 126)
		{
			header.mExtendedLength16 = 0;
			header.mExtendedLength16 |= ((uint16_t) dataPtr[2]) << 8;
			header.mExtendedLength16 |= ((uint16_t) dataPtr[3]) << 0;
			headerOffset = 4;
		}
		else if(header.mInitialLength == 127)
		{
			header.mExtendedLength = 0;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[2]) << 56;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[3]) << 48;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[4]) << 40;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[5]) << 32;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[6]) << 24;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[7]) << 16;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[8]) << 8;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[9]) << 0;
			headerOffset = 10;
		}

		if(header.mIsMasked)
		{
			header.mMask[0] = (static_cast<uint8_t>(dataPtr[headerOffset+0])) << 0;
			header.mMask[1] = (static_cast<uint8_t>(dataPtr[headerOffset+1])) << 0;
			header.mMask[2] = (static_cast<uint8_t>(dataPtr[headerOffset+2])) << 0;
			header.mMask[3] = (static_cast<uint8_t>(dataPtr[headerOffset+3])) << 0;
		}
		else
		{
			header.mMask[0] = 0;
			header.mMask[1] = 0;
			header.mMask[2] = 0;
			header.mMask[3] = 0;
		}

		if(header.mOpcode == Opcode::TEXT
		|| header.mOpcode == Opcode::BINARY
		|| header.mOpcode == Opcode::CONTINUATION)
		{
			if(header.mFin)
			{
				if(header.mIsMasked)
				{
					for(size_t i = 0; i != header.mExtendedLength; ++i)
					{
						data[i+headerOffset] ^= header.mMask[i&0x3];
					}
				}
				if(header.mInitialLength == 126)
				{
					mHandler->OnIncomingData(this, data+header.mHeaderLength, header.mExtendedLength16);
					if(header.mExtendedLength16+header.mHeaderLength < len)
					{
						mLogger->debug("Packet with multiple websocket packets detected");
						OnIncomingData(conn, data+header.mExtendedLength16+header.mHeaderLength, header.mExtendedLength16+header.mHeaderLength);
					}
				}
				else
				{
					mHandler->OnIncomingData(this, data+header.mHeaderLength, len-header.mHeaderLength);
				}
			}
			else
			{
				mLogger->warn("Incoming packet not fin");
			}
		}
		else if(header.mOpcode == Opcode::CLOSE)
		{
			mLogger->warn("Incoming close");
			mHandler->OnDisconnect(this);
		}
		else
		{
			mLogger->warn("Unhandled opcode: {}", dataPtr[0] & 0x0F);
		}
	}

	void StartWebsocketConnection()
	{
		std::stringstream ss;
		ss << "GET /" << mUrl << " HTTP/1.1\r\n"
		<< "Upgrade: websocket\r\n"
		<< "Connection: Upgrade\r\n"
		<< "Host: " << mAddress << ":" << mPort << "\r\n"
		<< "Sec-WebSocket-Key: nsc4bhaU3ax5CeMGPKiPVA== \r\n"
		<< "Sec-Websocket-Version: 13\r\n"
		<< "\r\n";
		std::string connectString = ss.str();

		mSocket.Send(connectString.c_str(), connectString.size());
	}

	EventLoop::EventLoop& mEventLoop;
	IWebsocketClientHandler<SocketType, Handler>* mHandler;

	SocketType mSocket;

	uint16_t mPort;
	std::string mAddress;
	std::string mUrl;

	bool mConnected;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // WEBSOCKET_H
