#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "EventLoop.h"
#include "StreamSocket.h"
#include "TLSSocket.h"

namespace Common {

using namespace EventLoop;

namespace Websocket {

struct WebsocketDecoder
{
public:
	WebsocketDecoder(EventLoop::EventLoop& ev, const std::uint8_t* buffer, const std::size_t bufferSize)
		: mBuffer(buffer)
		, mBufferSize(bufferSize)
		, mIndex(0)
	{
		mLogger = ev.RegisterLogger("WebsocketDecoder");
	}

	WebsocketDecoder(EventLoop::EventLoop& ev, const std::vector<std::uint8_t>& buffer)
		: mBuffer(buffer.data())
		, mBufferSize(buffer.size())
		, mIndex(0)
		{
			mLogger = ev.RegisterLogger("WebsocketDecoder");
		}

	enum class Opcode : std::uint8_t {
		CONTINUATION = 0x0,
		TEXT = 0x1,
		BINARY = 0x2,
		CLOSE = 0x8,
		PING = 0x9,
		PONG = 0xA,
	};

	enum class MessageState : std::uint8_t {
		NoMessage,
		TextMessage,
		BinaryMessage,
		Error,
		Ping,
		Pong,
		Close
	};

	MessageState DecodeMessage(std::vector<std::uint8_t>& dest)
	{
		if (mIndex + 1 >= mBufferSize)
		{
			return MessageState::NoMessage;
		}
		if ((mBuffer[mIndex] & 0x80) == 0)
		{
			// FIN bit is not clear...
			mLogger->warn("Received websocket frame without FIN bit set - unsupported");
			return MessageState::Error;
		}

		const auto reservedBits = mBuffer[mIndex] & (7 << 4);
		if ((reservedBits & 0x30) != 0)
		{
			mLogger->warn("Received websocket frame with reserved bits set - error");
			return MessageState::Error;
		}

		const auto opcode = static_cast<Opcode>(mBuffer[mIndex] & 0xf);
		size_t payloadLength = mBuffer[mIndex + 1] & 0x7fu;
		const auto maskBit = mBuffer[mIndex + 1] & 0x80;
		auto ptr = mIndex + 2;
		if (payloadLength == 126)
		{
			if (mBufferSize < 4)
			{
				return MessageState::NoMessage;
			}
			uint16_t raw_length;
			std::memcpy(&raw_length, &mBuffer[ptr], sizeof(raw_length));
			payloadLength = htons(raw_length);
			ptr += 2;
		}
		else if (payloadLength == 127)
		{
			if (mBufferSize < 10)
			{
				return MessageState::NoMessage;
			}
			std::uint64_t raw_length;
			std::memcpy(&raw_length, &mBuffer[ptr], sizeof(raw_length));
			payloadLength = __bswap_64(raw_length);
			ptr += 8;
		}
		uint32_t mask = 0;
		if (maskBit)
		{
			// MASK is set.
			if (mBufferSize < ptr + 4) {
				return MessageState::NoMessage;
			}
			std::uint32_t raw_length;
			std::memcpy(&raw_length, &mBuffer[ptr], sizeof(raw_length));
			mask = htonl(raw_length);
			ptr += 4;
		}
		auto bytesLeftInBuffer = mBufferSize - ptr;
		if (payloadLength > bytesLeftInBuffer)
		{
			return MessageState::NoMessage;
		}

		dest.clear();
		dest.reserve(payloadLength);
		for (auto i = 0u; i < payloadLength; ++i)
		{
			auto byteShift = (3 - (i & 3)) * 8;
			dest.push_back(static_cast<uint8_t>((mBuffer[ptr++] ^ (mask >> byteShift)) & 0xff));
		}
		mIndex = ptr;
		switch (opcode) {
		default:
			mLogger->warn("Received hybi frame with unknown opcode: {}", static_cast<int>(opcode));
			return MessageState::Error;
		case Opcode::TEXT:
			return MessageState::TextMessage;
		case Opcode::BINARY:
			return MessageState::BinaryMessage;
		case Opcode::PING:
			return MessageState::Ping;
		case Opcode::PONG:
			return MessageState::Pong;
		case Opcode::CLOSE:
			return MessageState::Close;
		}
	}

	std::size_t GetNumBytesDecoded() const
	{
		return mIndex;
	}

private:
	std::shared_ptr<spdlog::logger> mLogger;
	const std::uint8_t* mBuffer;
	std::size_t mIndex;
	const std::size_t mBufferSize;
};

} // namespace Websocket

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

	enum class Opcode : std::uint8_t {
		CONTINUATION = 0x0,
		TEXT = 0x1,
		BINARY = 0x2,
		CLOSE = 0x8,
		PING = 0x9,
		PONG = 0xA,
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
			mSocket.Shutdown();
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

		Websocket::WebsocketDecoder decoder(mEventLoop, dataPtr, len);
		Websocket::WebsocketDecoder::MessageState state;
		std::vector<std::uint8_t> dest;
		dest.reserve(512);
		while(true)
		{
			state = decoder.DecodeMessage(dest);
			switch(state)
			{
			case Websocket::WebsocketDecoder::MessageState::TextMessage:
			{
				mHandler->OnIncomingData(this, (char*)dest.data(), dest.size());
				break;
			}
			case Websocket::WebsocketDecoder::MessageState::BinaryMessage:
			{
				mHandler->OnIncomingData(this, (char*)dest.data(), dest.size());
				break;
			}
			case Websocket::WebsocketDecoder::MessageState::NoMessage:
			{
				mLogger->warn("NoMessage");
				return;
			}
			case Websocket::WebsocketDecoder::MessageState::Error:
			{
				mLogger->critical("Error state");
				throw std::runtime_error("error state returned from websocket decoder");
				return;
			}
			default:
			{
				mLogger->warn("Unhandled messagestate");
				return;
			}
			}
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
