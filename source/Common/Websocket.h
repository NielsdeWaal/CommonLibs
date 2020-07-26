#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "EventLoop.h"
#include "StreamSocket.h"
#include "TLSSocket.h"

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
	virtual void OnIncomingData(WebsocketClient<SocketType, Handler>* conn, char* data, size_t len) = 0;
	virtual ~IWebsocketClientHandler() = default;
	// IWebsocketClientHandler(const IWebsocketClientHandler&) = delete;
	// IWebsocketClientHandler& operator=(const IWebsocketClientHandler&) = delete;
	// IWebsocketClientHandler(IWebsocketClientHandler&&) = delete;
	// IWebsocketClientHandler& operator=(IWebsocketClientHandler&&) = delete;
};

/**
 * @brief simple class for setting up client websocket connection
 *
 * TODO We need a more clean way for specifing the port, maybe default to either 80 of 443 depending on the SocketType, and if specified use another port.
 * TODO Better connect string solution. Now we just construct one roughly without any thought.
 * TODO Ping timeout, maybe with config option. Just a normal eventloop timer should suffice.
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
		std::array<uint8_t,4> mMask;
	};

public:
	WebsocketClient(EventLoop::EventLoop& ev, IWebsocketClientHandler<SocketType, Handler>* handler) noexcept
		: mEventLoop(ev)
		, mHandler(handler)
		, mSocket(ev, this)
		, mPort(0)
		, mConnected(false)
	{
		mLogger = mEventLoop.RegisterLogger("WebsocketClient");
	}

	~WebsocketClient()
	{
		if(mConnected)
		{
			SendControlMessage(Opcode::CLOSE, "", 0);
		}
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

	/**
	 * @brief Send connection request using hostname.
	 */
	void ConnectHostname(const std::string& hostname, const std::uint16_t port, const std::string& additionalURL)
	{
		mSocket.ConnectHostname(hostname, port);
		mPort = port;
		mAddress = hostname;
		mUrl = additionalURL;
	}

	/**
	 * @brief Send data to connected host
	 *
	 * `len` must be equal to the data that is supposed to be send.
	 * Currently we are masking every packet with the same key.
	 */
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

	/**
	 * @brief Send ping request
	 *
	 * Additional data can be supplied and will be verified upon receiving a pong message.
	 * !!! Currently there is no timeout mechanism in place, it should still be usefull as a heartbeat mechanism
	 */
	void SendPing(const char* data, const size_t len)
	{
		if(mConnected)
		{
			SendControlMessage(Opcode::PING, data, len);
			mPingData = std::make_unique<char[]>(len);
			std::copy(data, data+len, mPingData.get());
			mAwaitingPong = true;
		}
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
		if(mFragmented)
		{
			mLogger->warn("Received fragment");
			if(len > mRemainingSize)
			{
				mLogger->warn("Packet contains both fragment and next packet");
				for(std::size_t i = 0; i < mRemainingSize; ++i)
				{
					mFragmentation.push_back(data[i]);
				}
				data += mRemainingSize;
				mRemainingSize = 0;
			}
			else
			{
				mRemainingSize -= len;
				mLogger->warn("    Inc fragment: {}", std::string{data, len});
				mLogger->warn("    Remaining bytes: {}", mRemainingSize);
				for(std::size_t i = 0; i < len; ++i)
				{
					mFragmentation.push_back(data[i]);
				}
				if(mRemainingSize == 0)
				{
					mFragmented = false;
					mHandler->OnIncomingData(this, mFragmentation.data(), mFragmentation.size());
				}
				return;
			}
		}
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
		header.mFin = dataPtr[0] & 128;
		header.mOpcode = static_cast<Opcode>(dataPtr[0] & 15);
		header.mIsMasked = (dataPtr[1] & 0x80) == 0x80;
		header.mInitialLength = (dataPtr[1] & 0x7F);
		header.mHeaderLength = 2 // Opcode and reserved bits
			+ (header.mInitialLength == 126 ? 2 : 0) // Standard length
			+ (header.mInitialLength == 127 ? 8 : 0) // Extended length
			+ (header.mIsMasked? 4 : 0); // Masking key
		header.mExtendedLength = 0;

		size_t headerOffset = 0;

		if(header.mInitialLength < 126)
		{
			header.mExtendedLength = header.mInitialLength;
			headerOffset = 2;
		}
		else if(header.mInitialLength == 126)
		{
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[2]) << 8;
			header.mExtendedLength |= static_cast<uint64_t>(dataPtr[3]) << 0;
			headerOffset = 4;
		}
		else if(header.mInitialLength == 127)
		{
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

		// CONTINUATION woes
		// Continuation frames are part of fragmentend frames.
		// These flow as follows:
		//  - First frame has either TEXT or BINARY, FIN bit is clear (0)
		//  - Subsequent frames has CONTINUATION opcode, FIN bit is clear (0)
		//  - Final frame has CONTINUATION opcode, FIN bit is set (1)
		if(header.mOpcode == Opcode::TEXT
		|| header.mOpcode == Opcode::BINARY
		|| header.mOpcode == Opcode::CONTINUATION)
		{
			if(header.mFin)
			{
				if(len != header.mHeaderLength+header.mExtendedLength)
				{
					mLogger->warn("Fragmentend message found, len: {}, required size: {}+{}", len, header.mHeaderLength, header.mExtendedLength);
					if(header.mHeaderLength + header.mExtendedLength < len)
					{
						mLogger->warn("    Fragment is smaller then len");
						mHandler->OnIncomingData(this, data+header.mHeaderLength, header.mExtendedLength);
						OnIncomingData(conn, data+header.mHeaderLength+header.mExtendedLength, len-header.mExtendedLength-header.mHeaderLength);
						return;
					}
					else
					{
						mRemainingSize = (header.mHeaderLength+header.mExtendedLength) - len;
						mLogger->warn("    Remaining bytes: {}", mRemainingSize);
						mFragmented = true;
						for(std::size_t i = header.mHeaderLength; i < len; ++i)
						{
							mFragmentation.push_back(data[i]);
						}
						return;
					}
				}
				if(mContinuationFragments)
				{
					mContinuationFragments = false;
					for(std::size_t i = 0; i < mContinuation.size(); ++i)
					{
						mContinuation[i+headerOffset] ^= header.mMask[i&0x3];
					}
					mHandler->OnIncomingData(this, mContinuation.data(), mContinuation.size());
					mContinuation.clear();
				}
				else
				{
					for(std::size_t i = 0; i != header.mExtendedLength; ++i)
					{
						data[i+headerOffset] ^= header.mMask[i&0x3];
					}
					mHandler->OnIncomingData(this, data+header.mHeaderLength, header.mExtendedLength);
					if(header.mExtendedLength+header.mHeaderLength < len)
					{
						mLogger->debug("Packet with multiple websocket packets detected");
						OnIncomingData(conn, data+header.mExtendedLength+header.mHeaderLength, header.mExtendedLength+header.mHeaderLength);
					}
				}
			}
			else
			{
				mContinuationFragments = true;
				for(std::size_t i = 0; i != header.mExtendedLength-header.mHeaderLength; ++i)
				{
					mContinuation.push_back(data[i+header.mHeaderLength]);
				}
				mLogger->warn("Incoming packet not fin");
			}
		}
		else if(header.mOpcode == Opcode::CLOSE)
		{
			mLogger->warn("Incoming close");
			mHandler->OnDisconnect(this);
			mConnected = false;
		}
		else if(header.mOpcode == Opcode::PING)
		{
			mLogger->debug("Incoming ping request");
			if(mConnected)
			{
				SendControlMessage(Opcode::PONG, data+header.mHeaderLength, header.mExtendedLength);
			}
			/* Respond with pong */
		}
		else if(header.mOpcode == Opcode::PONG)
		{
			mLogger->debug("Incoming pong message");
			if(mPingData)
			{
				const int rc = std::memcmp(data+header.mHeaderLength, mPingData.get(), header.mExtendedLength);
				if(rc != 0)
				{
					mLogger->error("Pong reply does not equal");
					mHandler->OnDisconnect(this);
					mConnected = false;
					mPingData.reset(nullptr);
					mAwaitingPong = false;
				}
			}
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

	// TODO should be merged with the normal send function as this is just a duplicate
	void SendControlMessage(Opcode packetOpcode, const char* data, const size_t len)
	{
		assert(
		packetOpcode == Opcode::PING ||
		packetOpcode == Opcode::PONG ||
		packetOpcode == Opcode::CLOSE);

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

		header[0] = 0x80 | static_cast<int>(packetOpcode);

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

	EventLoop::EventLoop& mEventLoop;
	IWebsocketClientHandler<SocketType, Handler>* mHandler;

	SocketType mSocket;

	uint16_t mPort;
	std::string mAddress;
	std::string mUrl;

	bool mConnected;

	bool mContinuationFragments = false;
	std::vector<char> mContinuation;

	bool mFragmented = false;
	std::size_t mRemainingSize = 0;
	std::vector<char> mFragmentation;

	std::unique_ptr<char[]> mPingData;
	// TODO currenty this isn't used, but this should be used in a timeout
	bool mAwaitingPong = false;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace Common

#endif // WEBSOCKET_H
