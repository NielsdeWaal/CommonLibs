#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "EventLoop.h"
#include "StreamSocket.h"
#include "TLSSocket.h"
#include <algorithm>
#include <bits/c++config.h>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace Common {

using namespace EventLoop;

namespace Websocket {

struct WebsocketDecoder
{
public:
	explicit WebsocketDecoder(EventLoop::EventLoop& ev)
		: mRemaining(BASIC_HEADER_LENGTH)
		, mState(State::HEADER_BASIC)
	{
		mLogger = ev.RegisterLogger("WebsocketDecoder");
	}
	enum Opcode : std::uint8_t
	{
		CONTINUATION = 0x0,
		TEXT = 0x1,
		BINARY = 0x2,
		CLOSE = 0x8,
		PING = 0x9,
		PONG = 0xA,
	};

	static inline bool IsControl(Opcode op)
	{
		return op >= 0x8;
	}

	enum class MessageState : std::uint8_t
	{
		NoMessage,
		PartialMessage,
		TextMessage,
		BinaryMessage,
		Error,
		Ping,
		Pong,
		Close
	};

	enum class State : std::uint8_t
	{
		HEADER_BASIC = 0,
		HEADER_EXTENDED = 1,
		EXTENSION = 2,
		APPLICATION = 3,
		READY = 4,
		FATAL_ERROR = 5
	};

	// TODO move to (websocket) common file
	struct BasicHeader
	{
		BasicHeader()
			: Byte0(0x00)
			, Byte1(0x00)
		{}

		BasicHeader(std::uint8_t b0, std::uint8_t b1)
			: Byte0(b0)
			, Byte1(b1)
		{}

		std::uint8_t Byte0;
		std::uint8_t Byte1;
	};

	// TODO move to (websocket) util file
	union uint16_converter
	{
		std::uint16_t i;
		// std::array<std::uint8_t, 2> c;
		std::uint8_t c[2];
	};

	// TODO move to (websocket) util file
	union uint32_converter
	{
		std::uint32_t i;
		// std::array<std::uint8_t, 4> c;
		std::uint8_t c[4];
	};

	// TODO move to (websocket) util file
	union uint64_converter
	{
		std::uint64_t i;
		// std::array<std::uint8_t, 8> c;
		std::uint8_t c[8];
	};
	// TODO move to (websocket) util file
	// TODO use variant
	static constexpr std::size_t TYP_INIT = 0;
	static constexpr std::size_t TYP_SMLE = 1;
	static constexpr std::size_t TYP_BIGE = 2;
	inline static std::uint64_t _htonll(std::uint64_t src)
	{
		static int typ = TYP_INIT;
		unsigned char c;
		union
		{
			std::uint64_t ull;
			unsigned char c[8];
		} x;
		if(typ == TYP_INIT)
		{
			x.ull = 0x01;
			typ = (x.c[7] == 0x01ULL) ? TYP_BIGE : TYP_SMLE;
		}
		if(typ == TYP_BIGE)
			return src;
		x.ull = src;
		c = x.c[0];
		x.c[0] = x.c[7];
		x.c[7] = c;
		c = x.c[1];
		x.c[1] = x.c[6];
		x.c[6] = c;
		c = x.c[2];
		x.c[2] = x.c[5];
		x.c[5] = c;
		c = x.c[3];
		x.c[3] = x.c[4];
		x.c[4] = c;
		return x.ull;
	}
	inline static std::uint64_t _ntohll(std::uint64_t src)
	{
		return _htonll(src);
	}

	inline static bool GetFin(const BasicHeader& b)
	{
		return (b.Byte0 & BHB0_FIN) == BHB0_FIN;
	}

	// TODO move to (websocket) common file
	static constexpr std::size_t BASIC_HEADER_LENGTH = 2;
	static constexpr std::size_t MAX_EXTENDED_HEADER_LENGTH = 12;
	static constexpr std::uint8_t PAYLOAD_SIZE_BASIC = 125;
	static constexpr std::uint8_t BHB0_OPCODE = 15;
	static constexpr std::uint8_t BHB0_FIN = 128;
	static constexpr std::uint8_t BHB1_PAYLOAD = 127;
	static constexpr std::uint16_t PAYLOAD_SIZE_EXTENDED = 0xFFFF;
	static constexpr std::uint16_t PAYLOAD_SIZE_16BIT = 0x7E;
	static constexpr std::size_t MAX_MESSAGE_SIZE = 16000000;
	struct ExtendedHeader
	{
		ExtendedHeader()
		{
			std::fill_n(mBytes, MAX_EXTENDED_HEADER_LENGTH, 0x00);
		}
		ExtendedHeader(std::uint64_t payload_size)
		{
			std::fill_n(mBytes, MAX_EXTENDED_HEADER_LENGTH, 0x00);

			CopyPayload(payload_size);
		}

		ExtendedHeader(std::uint64_t payloadSize, std::uint32_t maskingKey)
		{
			std::fill_n(mBytes, MAX_EXTENDED_HEADER_LENGTH, 0x00);

			// Copy payload size
			int offset = CopyPayload(payloadSize);

			// Copy Masking Key
			uint32_converter temp32;
			temp32.i = maskingKey;
			std::copy(temp32.c, temp32.c + 4, mBytes + offset);
		}

		std::uint8_t mBytes[MAX_EXTENDED_HEADER_LENGTH];

	private:
		int CopyPayload(std::uint64_t payload_size)
		{
			int payloadOffset = 0;

			if(payload_size <= PAYLOAD_SIZE_BASIC)
			{
				payloadOffset = 8;
			}
			else if(payload_size <= PAYLOAD_SIZE_EXTENDED)
			{
				payloadOffset = 6;
			}

			uint64_converter temp64{};
			temp64.i = _htonll(payload_size);
			std::copy(temp64.c + payloadOffset, temp64.c + 8, mBytes);

			return 8 - payloadOffset;
		}
	};

	std::size_t CopyBasicHeaderBytes(const std::uint8_t* buf, std::size_t len)
	{
		if(len == 0 || mRemaining == 0)
		{
			return 0;
		}

		if(len > 1)
		{
			// have at least two bytes
			if(mRemaining == 2)
			{
				mBasicHeader.Byte0 = buf[0];
				mBasicHeader.Byte1 = buf[1];
				mRemaining -= 2;
				return 2;
			}
			else
			{
				mBasicHeader.Byte1 = buf[0];
				mRemaining--;
				return 1;
			}
		}
		else
		{
			// have exactly one byte
			if(mRemaining == 2)
			{
				mBasicHeader.Byte0 = buf[0];
				mRemaining--;
				return 1;
			}
			else
			{
				mBasicHeader.Byte1 = buf[0];
				mRemaining--;
				return 1;
			}
		}
	}

	std::size_t CopyExtendedHeaderBytes(const std::uint8_t* buf, std::size_t len)
	{
		std::size_t toRead = (std::min)(mRemaining, len);

		std::copy(buf, buf + mRemaining, mExtendedHeader.mBytes + mCursor);
		mCursor += toRead;
		mRemaining -= toRead;

		return toRead;
	}

	static inline bool GetMasked(const BasicHeader& header)
	{
		return ((header.Byte1 & 128) == 128);
	}

	static inline std::uint64_t GetMaskingKeyOffset(const BasicHeader& header)
	{
		if(GetBasicSize(header) == 126)
		{
			return 2;
		}
		else if(GetBasicSize(header) == 127)
		{
			return 8;
		}
		else
		{
			return 0;
		}
	}

	static inline std::size_t GetHeaderLength(const BasicHeader& header)
	{
		std::size_t size = 2 + GetMaskingKeyOffset(header);

		if(GetMasked(header))
		{
			size += 4;
		}
		return size;
	}

	inline static std::uint8_t GetBasicSize(const BasicHeader& h)
	{
		return h.Byte1 & BHB1_PAYLOAD;
	}

	static inline std::uint16_t GetExtendedSize(const ExtendedHeader& e)
	{
		uint16_converter temp16{};
		std::copy(e.mBytes, e.mBytes + 2, temp16.c);
		return ntohs(temp16.i);
	}

	static inline std::uint64_t GetJumboSize(const ExtendedHeader& e)
	{
		uint64_converter temp64{};
		std::copy(e.mBytes, e.mBytes + 8, temp64.c);
		return _ntohll(temp64.i);
	}

	static inline std::uint64_t GetPayloadSize(const BasicHeader h, const ExtendedHeader& e)
	{
		std::uint8_t val = GetBasicSize(h);

		if(val <= PAYLOAD_SIZE_BASIC)
		{
			return val;
		}
		else if(val == PAYLOAD_SIZE_16BIT)
		{
			return GetExtendedSize(e);
		}
		else
		{
			return GetJumboSize(e);
		}
	}

	static inline Opcode GetOpcode(const BasicHeader& h)
	{
		return static_cast<Opcode>(h.Byte0 & BHB0_OPCODE);
	}

	static inline uint32_converter GetMaskingKey(const BasicHeader& h, const ExtendedHeader& e)
	{
		uint32_converter temp32{};

		if(!GetMasked(h))
		{
			temp32.i = 0;
		}
		else
		{
			std::size_t offset = GetMaskingKeyOffset(h);
			std::copy(e.mBytes + offset, e.mBytes + offset + 4, temp32.c);
		}

		return temp32;
	}

	struct Message
	{
	public:
		explicit Message(Opcode op, std::size_t size = 128)
			: mOpcode(op)
			, mPrepared(false)
			, mFin(false)
			, mTerminal(false)
		{
			// FIXME This should be used at some point, see if actually required and not taken care of inside
			// mPayload.resize(size);
		}

		std::string& GetRawPayload()
		{
			return mPayload;
		}

		Opcode GetOpcode() const
		{
			return mOpcode;
		}

		[[nodiscard]] const std::string& GetPayload() const
		{
			return mPayload;
		}

	private:
		std::string mHeader;
		std::string mPayload;
		Opcode mOpcode;
		bool mPrepared;
		bool mFin;
		bool mTerminal;
	};

	struct MessageWrapper
	{
		MessageWrapper() = default;
		MessageWrapper(std::unique_ptr<Message> msg, std::size_t prepKey)
			: mMsg(std::move(msg))
			, mPrepKey(prepKey)
		{}
		MessageWrapper(std::unique_ptr<Message> msg, uint32_converter prepKey)
			: mMsg(std::move(msg))
			, mPrepKey(PrepareMaskingKey(prepKey))
		{}

		std::unique_ptr<Message> mMsg;
		std::size_t mPrepKey;
	};

	static inline bool IsLittleEndian()
	{
		short int val = 0x1;
		char* ptr = reinterpret_cast<char*>(&val);
		return (ptr[0] == 1);
	}

	static inline std::size_t CircshiftPrepKey(std::size_t prepKey, std::size_t offset)
	{
		if(offset == 0)
		{
			return prepKey;
		}
		if(IsLittleEndian())
		{
			std::size_t temp = prepKey << (sizeof(std::size_t) - offset) * 8;
			return (prepKey >> offset * 8) | temp;
		}
		else
		{
			std::size_t temp = prepKey >> (sizeof(std::size_t) - offset) * 8;
			return (prepKey << offset * 8) | temp;
		}
	}

	static inline std::size_t ByteMaskCirc(
		std::uint8_t* input, std::uint8_t* output, std::size_t length, std::size_t prepKey)
	{
		uint32_converter key{};
		key.i = prepKey;

		for(std::size_t i = 0; i < length; ++i)
		{
			output[i] = input[i] ^ key.c[i % 4];
		}

		return CircshiftPrepKey(prepKey, length % 4);
	}

	static inline std::size_t ByteMaskCirc(std::uint8_t* buf, std::size_t len, std::size_t prepKey)
	{
		return ByteMaskCirc(buf, buf, len, prepKey);
	}

	static inline std::string PrepareHeader(const BasicHeader& h, const ExtendedHeader& e)
	{
		std::string ret;

		ret.push_back(char(h.Byte0));
		ret.push_back(char(h.Byte1));
		ret.append(reinterpret_cast<const char*>(e.mBytes), GetHeaderLength(h) - BASIC_HEADER_LENGTH);

		return ret;
	}

	template<typename InputIter, typename OutputIter>
	static void ByteMask(
		InputIter first, InputIter last, OutputIter res, const uint32_converter& key, std::size_t keyOffset = 0)
	{
		std::size_t index = keyOffset % 4;
		while(first != last)
		{
			*res = *first ^ key.c[index++];
			index %= 4;
			++res;
			++first;
		}
	}

	template<typename IterType>
	static void ByteMask(IterType first, IterType last, const uint32_converter& key, std::size_t keyOffset = 0)
	{
		ByteMask(first, last, first, key, keyOffset);
	}

	// TODO Have different functions for when payload size is big or small
	// Makes for easier forwarding. Either forward if packet is fully received in one go.
	std::size_t ProcessPayloadBytes(std::uint8_t* buf, std::size_t len)
	{
		if(GetMasked(mBasicHeader))
		{
			mCurrentMessage->mPrepKey = ByteMaskCirc(buf, len, mCurrentMessage->mPrepKey);
		}

		std::string& out = mCurrentMessage->mMsg->GetRawPayload();

		out.append(reinterpret_cast<char*>(buf), len);

		mRemaining -= len;

		return len;
	}

	static inline std::size_t PrepareMaskingKey(const uint32_converter& key)
	{
		std::size_t lowBits = static_cast<std::size_t>(key.i);

		if(sizeof(std::size_t) == 8)
		{
			std::uint64_t highBits = static_cast<std::size_t>(key.i);
			return static_cast<size_t>((highBits << 32) | lowBits);
		}
		else
		{
			return lowBits;
		}
	}

	void ResetHeaders()
	{
		mState = State::HEADER_BASIC;
		mRemaining = BASIC_HEADER_LENGTH;

		mBasicHeader.Byte0 = 0x00;
		mBasicHeader.Byte1 = 0x00;

		std::fill_n(mExtendedHeader.mBytes, MAX_EXTENDED_HEADER_LENGTH, 0x00);
	}

	void FinalizeMessage()
	{
		mState = State::READY;
	}

	std::unique_ptr<Message> GetMessage()
	{
		if(!Ready())
		{
			return nullptr;
		}
		std::unique_ptr<Message> ret = std::move(mCurrentMessage->mMsg);
		mCurrentMessage->mMsg.reset();

		ResetHeaders();

		return ret;
	}

	std::size_t ConsumeMessage(std::uint8_t* buf, std::size_t len)
	{
		std::size_t p = 0;

		while(mState != State::READY && mState != State::FATAL_ERROR && (p < len || mRemaining == 0))
		{
			if(mState == State::HEADER_BASIC)
			{
				p += CopyBasicHeaderBytes(buf + p, len - p);
				if(mRemaining > 0)
				{
					continue;
				}

				mState = State::HEADER_EXTENDED;
				mCursor = 0;
				mRemaining = GetHeaderLength(mBasicHeader) - BASIC_HEADER_LENGTH;
			}
			else if(mState == State::HEADER_EXTENDED)
			{
				p += CopyExtendedHeaderBytes(buf + p, len - p);
				if(mRemaining > 0)
				{
					continue;
				}

				mState = State::APPLICATION;
				mRemaining = static_cast<std::size_t>(GetPayloadSize(mBasicHeader, mExtendedHeader));
				Opcode op = GetOpcode(mBasicHeader);

				if(IsControl(op))
				{
					mControlMessage = std::make_unique<MessageWrapper>(std::make_unique<Message>(op, mRemaining), 0);

					mCurrentMessage = std::move(mControlMessage);
					mCurrentIsDataMsg = false;
				}
				else
				{
					if(!mDataMessage)
					{
						if(mRemaining > MAX_MESSAGE_SIZE)
						{
							mLogger->critical("Message size too big");
							throw std::runtime_error("Remaining size is too large");
						}

						mLogger->info("Creating new message with size: {}", mRemaining);

						// FIXME prepkey in wrapper
						mDataMessage = std::make_unique<MessageWrapper>(
							std::make_unique<Message>(op, mRemaining), GetMaskingKey(mBasicHeader, mExtendedHeader));
					}
					else
					{
						std::string& out = mDataMessage->mMsg->GetRawPayload();
						if(out.size() + mRemaining > MAX_MESSAGE_SIZE)
						{
							mLogger->critical("Message size plus out payload size too big");
							throw std::runtime_error("Remaining size is too large");
						}

						mDataMessage->mPrepKey = PrepareMaskingKey(GetMaskingKey(mBasicHeader, mExtendedHeader));
						out.reserve(out.size() + mRemaining);
					}
					mCurrentMessage = std::move(mDataMessage);
					mCurrentIsDataMsg = true;
				}
			}
			else if(mState == State::APPLICATION)
			{
				const std::size_t bytesToProcess = (std::min)(mRemaining, len - p);
				if(bytesToProcess > 0)
				{
					p += ProcessPayloadBytes(buf + p, bytesToProcess);
				}

				if(mRemaining > 0)
				{
					continue;
				}

				if(GetFin(mBasicHeader))
				{
					FinalizeMessage();
					break;
				}
				else
				{
					ResetHeaders();
					if(mCurrentIsDataMsg)
					{
						mDataMessage = std::move(mCurrentMessage);
					}
					else
					{
						mControlMessage = std::move(mCurrentMessage);
					}
				}
			}
			else
			{
				mLogger->critical("Unknown state, aborting");
				throw std::runtime_error("Unknown state");
			}
		}
		return p;
	}

	[[nodiscard]] bool Ready() const
	{
		return (mState == State::READY);
	}

private:
	std::shared_ptr<spdlog::logger> mLogger;

	std::size_t mRemaining;
	std::size_t mCursor;
	State mState;
	BasicHeader mBasicHeader;
	ExtendedHeader mExtendedHeader;
	std::unique_ptr<MessageWrapper> mCurrentMessage;
	std::unique_ptr<MessageWrapper> mControlMessage;
	std::unique_ptr<MessageWrapper> mDataMessage;
	bool mCurrentIsDataMsg;
};

} // namespace Websocket

template<typename SocketType, typename Handler>
class WebsocketClient;

template<typename SocketType, typename Handler>
class IWebsocketClientHandler
{
private:
	static_assert(std::is_same_v<Common::StreamSocket, SocketType> || std::is_same_v<Common::TLSSocket, SocketType>,
		"SocketType is not compatible with websocket");

public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect([[maybe_unused]] WebsocketClient<SocketType, Handler>* conn) = 0;
	virtual void OnIncomingData(
		[[maybe_unused]] WebsocketClient<SocketType, Handler>* conn, const char* data, size_t len) = 0;
	virtual ~IWebsocketClientHandler() = default;
	// IWebsocketClientHandler(const IWebsocketClientHandler&) = delete;
	// IWebsocketClientHandler& operator=(const IWebsocketClientHandler&) = delete;
	// IWebsocketClientHandler(IWebsocketClientHandler&&) = delete;
	// IWebsocketClientHandler& operator=(IWebsocketClientHandler&&) = delete;
};

/**
 * @brief simple class for setting up client websocket connection
 *
 * TODO We need a more clean way for specifing the port, maybe default to either 80 of 443 depending on the SocketType,
 * and if specified use another port.
 * TODO Better connect string solution. Now we just construct one roughly without any thought.
 * TODO Ping timeout, maybe with config option. Just a normal eventloop timer should suffice.
 * TODO Actuall HTTP handler
 */

template<typename SocketType, typename Handler>
class WebsocketClient : public Handler
{
private:
	static_assert(std::is_same_v<Common::StreamSocket, SocketType> || std::is_same_v<Common::TLSSocket, SocketType>,
		"SocketType is not compatible with websocket");

	enum class Opcode : std::uint8_t
	{
		CONTINUATION = 0x0,
		TEXT = 0x1,
		BINARY = 0x2,
		CLOSE = 0x8,
		PING = 0x9,
		PONG = 0xA,
	};

	struct WebsocketHeader
	{
		unsigned int mHeaderLength;
		bool mFin;
		bool mIsMasked;
		Opcode mOpcode;
		int mInitialLength;
		uint64_t mExtendedLength;
		std::array<uint8_t, 4> mMask;
	};

public:
	WebsocketClient(EventLoop::EventLoop& ev, IWebsocketClientHandler<SocketType, Handler>* handler) noexcept
		: mEventLoop(ev)
		, mHandler(handler)
		, mSocket(ev, this)
		, mPort(0)
		, mConnected(false)
		, mDecoder(ev)
	{
		mLogger = mEventLoop.RegisterLogger("WebsocketClient");
	}

	WebsocketClient(const WebsocketClient&) = delete;
	WebsocketClient& operator=(const WebsocketClient&) = delete;
	WebsocketClient(const WebsocketClient&&) = delete;
	WebsocketClient& operator=(const WebsocketClient&&) = delete;

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
		header.assign(2 + (len >= 126 ? 2 : 0) + (len >= 65536 ? 6 : 0), 0);

		std::string payload;
		payload.reserve(header.size() + len);
		const char* startPointer = data;
		const char* endPointer = startPointer + len;

		header[0] = 0x80 | static_cast<int>(Opcode::TEXT);

		header[1] = (len & 0xff) | (0x80);
		header.insert(std::begin(header) + 2, std::begin(maskingKey), std::end(maskingKey));

		// FIXME
		// Ugly as sin but it should work
		int i = 0;
		while(startPointer != endPointer)
		{
			payload.push_back((*startPointer++) ^ maskingKey[i++ % 4]);
		}
		header.insert(std::begin(header) + 2 + maskingKey.size(), std::begin(payload), std::end(payload));

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
			std::copy(data, data + len, mPingData.get());
			mAwaitingPong = true;
		}
	}

private:
	void OnConnected()
	{
		mLogger->info("Connected to TCP endpoint");
		StartWebsocketConnection();
	}

	void OnDisconnect([[maybe_unused]] SocketType* conn)
	{
		mLogger->warn("Connection to TCP endpoint closed");
	}

	void OnDisconnect(StreamSocket* conn)
	{
		mLogger->warn("Websocket testing server disconnected");
	}

	void OnIncomingData(SocketType* conn, char* data, size_t len)
	{
		// TODO Needs actual HTTP request handling instead of just looking at the first character
		if(data[0] == 'H' && !mConnected)
		{
			mLogger->info("Connection with websocket server established");
			mLogger->info("Incoming HTTP: {}", std::string{data});
			mConnected = true;
			mHandler->OnConnected();
			return;
		}

		mDecoder.ConsumeMessage((std::uint8_t*)data, len);
		if(mDecoder.Ready())
		{
			const auto msg = mDecoder.GetMessage();
			const auto& op = msg->GetOpcode();
			switch(op)
			{
			case Websocket::WebsocketDecoder::Opcode::BINARY:
				[[fallthrough]];
			case Websocket::WebsocketDecoder::Opcode::TEXT: {
				mHandler->OnIncomingData(this, msg->GetPayload().data(), msg->GetPayload().size());
				return;
			}
			case Websocket::WebsocketDecoder::Opcode::PING: {
				SendControlMessage(Opcode::PONG, msg->GetPayload().data(), msg->GetPayload().size());
				mLogger->info("Sending PONG");
			}
			case Websocket::WebsocketDecoder::Opcode::PONG: {
				mLogger->info("Received PONG");
				const int comp = std::strcmp(std::string{data, len}.data(), mPingData.get());
				if(comp != 0)
				{
					mLogger->error("PONG message returned wrong message, continuing for now");
				}
				mAwaitingPong = false;
			}
			}
		}
	}

	// TODO move request over to either FMT or actual HTTP handler
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
		assert(packetOpcode == Opcode::PING || packetOpcode == Opcode::PONG || packetOpcode == Opcode::CLOSE);

		if(!mConnected)
		{
			mLogger->warn("Attempted to send while not connected");
			return;
		}
		const std::vector<char> maskingKey{0x12, 0x34, 0x56, 0x78}; // Do we even care?
		std::vector<char> header;
		header.assign(2 + (len >= 126 ? 2 : 0) + (len >= 65536 ? 6 : 0), 0);

		std::string payload;
		const char* startPointer = data;
		const char* endPointer = startPointer + len;

		header[0] = 0x80 | static_cast<int>(packetOpcode);

		header[1] = (len & 0xff) | (0x80);
		header.insert(std::begin(header) + 2, std::begin(maskingKey), std::end(maskingKey));

		// FIXME
		// Ugly as sin but it should work
		int i = 0;
		while(startPointer != endPointer)
		{
			payload.push_back((*startPointer++) ^ maskingKey[i++ % 4]);
		}
		header.insert(std::begin(header) + 2 + maskingKey.size(), std::begin(payload), std::end(payload));

		mSocket.Send(header.data(), header.size());
	}

	EventLoop::EventLoop& mEventLoop;
	IWebsocketClientHandler<SocketType, Handler>* mHandler;

	SocketType mSocket;

	uint16_t mPort;
	std::string mAddress;
	std::string mUrl;

	bool mConnected;

	// Websocket::WebsocketDecoder mDecoder;

	bool mContinuationFragments = false;
	std::vector<char> mContinuation;

	bool mFragmented = false;
	std::size_t mRemainingSize = 0;
	std::vector<char> mFragmentation;

	std::unique_ptr<char[]> mPingData;
	// TODO currenty this isn't used, but this should be used in a timeout
	bool mAwaitingPong = false;

	Websocket::WebsocketDecoder mDecoder;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace Common

#endif // WEBSOCKET_H
