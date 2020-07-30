#include <catch2/catch.hpp>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <csignal>

#include "EventLoop.h"
#include "Websocket.h"

/**
 * @brief testcase for websocket connection
 * TODO currently the server side a raw tcp socket as the websocket server
 * is not yet complete
 */
// TEST_CASE("Websocket encoding", "[Eventloop websocket]")
// {
// 	using websocketClient = Common::WebsocketClient<Common::StreamSocket,Common::IStreamSocketHandler>;
// 	EventLoop::EventLoop loop;
// 	loop.LoadConfig("Example.toml");
// 	loop.Configure();

// 	struct Server : public Common::IStreamSocketServerHandler
// 		, public Common::IStreamSocketHandler
// 	{
// 		Server(EventLoop::EventLoop& ev, std::uint16_t bindPort)
// 			: mEv(ev)
// 			, mSocket(mEv, this)
// 			{
// 				mSocket.BindAndListen(bindPort);
// 			}

// 		IStreamSocketHandler* OnIncomingConnection() final
// 			{
// 				return this;
// 			}

// 		void OnConnected() final
// 			{}

// 		void OnDisconnect(Common::StreamSocket* conn) final
// 			{}

// 		void OnIncomingData(Common::StreamSocket* conn, char* data, std::size_t len) final
// 			{
// 				REQUIRE(conn->IsConnected());
// 				conn->Shutdown();
// 			}

// 	private:
// 		EventLoop::EventLoop& mEv;
// 		Common::StreamSocketServer mSocket;
// 		bool mWebsocketConnected = false;
// 		int mTestCounter = 0;
// 	};

// 	struct Client : public Common::IWebsocketClientHandler<Common::StreamSocket,Common::IStreamSocketHandler>
// 	{
// 	public:
// 		Client(EventLoop::EventLoop& ev)
// 			: mEv(ev)
// 			, mSocket(mEv, this)
// 		{
// 			mSocket.Connect("127.0.0.1", "", 1337);
// 		}

// 	private:
// 		EventLoop::EventLoop& mEv;
// 		websocketClient mSocket;
// 	};
// }

EventLoop::EventLoop loop;
void testSingleString(Common::Websocket::WebsocketDecoder::MessageState expectedState,
	const char* expectedPayload,
	const std::vector<uint8_t>& v,
	uint32_t size = 0)
{
	Common::Websocket::WebsocketDecoder decoder(loop, v);
	std::vector<uint8_t> decoded;
	CHECK(decoder.DecodeMessage(decoded) == expectedState);
	CHECK(std::string(reinterpret_cast<const char*>(&decoded[0]), decoded.size()) == expectedPayload);
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::NoMessage);
	CHECK(decoder.GetNumBytesDecoded() == (size ? size : v.size()));
}

void testLongString(size_t size, std::vector<uint8_t> v)
{
	for (std::size_t i = 0; i < size; ++i)
	{
		v.push_back('A');
	}
	Common::Websocket::WebsocketDecoder decoder(loop, v);
	std::vector<uint8_t> decoded;
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::TextMessage);
	REQUIRE(decoded.size() == size);
	for (size_t i = 0; i < size; ++i)
	{
		REQUIRE(decoded[i] == 'A');
	}
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::NoMessage);
	CHECK(decoder.GetNumBytesDecoded() == v.size());
}

TEST_CASE("textExamples", "[EventLoop Websocket]") {
	// CF. http://tools.ietf.org/html/draft-ietf-hybi-thewebsocketprotocol-10 #4.7
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::TextMessage, "Hello", {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::TextMessage, "Hello", {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58});
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::Ping, "Hello", {0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});
}

TEST_CASE("withPartialMessageFollowing", "[EventLoop Websocket]") {
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::TextMessage, "Hello", {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x81}, 7);
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::TextMessage, "Hello", {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x81, 0x05}, 7);
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::TextMessage, "Hello", {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x81, 0x05, 0x48}, 7);
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::TextMessage, "Hello", {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c}, 7);
}

TEST_CASE("binaryMessage", "[EventLoop Websocket]") {
	std::vector<uint8_t> packet{0x82, 0x03, 0x00, 0x01, 0x02};
	std::vector<uint8_t> expected_body{0x00, 0x01, 0x02};
	Common::Websocket::WebsocketDecoder decoder(loop, packet);
	std::vector<uint8_t> decoded;
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::BinaryMessage);
	CHECK(decoded == expected_body);
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::NoMessage);
	CHECK(decoder.GetNumBytesDecoded() == packet.size());
}

TEST_CASE("withTwoMessages", "[EventLoop Websocket]") {
	std::vector<uint8_t> data{
		0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f,
		0x81, 0x07, 0x47, 0x6f, 0x6f, 0x64, 0x62, 0x79, 0x65};
	Common::Websocket::WebsocketDecoder decoder(loop, data);
	std::vector<uint8_t> decoded;
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::TextMessage);
	CHECK(std::string(reinterpret_cast<const char*>(&decoded[0]), decoded.size()) == "Hello");
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::TextMessage);
	CHECK(std::string(reinterpret_cast<const char*>(&decoded[0]), decoded.size()) == "Goodbye");
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::NoMessage);
	CHECK(decoder.GetNumBytesDecoded() == data.size());
}

TEST_CASE("withTwoMessagesOneBeingMaskedd", "[EventLoop Websocket]") {
	std::vector<uint8_t> data{
		0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f,                        // hello
		0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 // also hello
	};
	Common::Websocket::WebsocketDecoder decoder(loop, data);
	std::vector<uint8_t> decoded;
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::TextMessage);
	CHECK(std::string(reinterpret_cast<const char*>(&decoded[0]), decoded.size()) == "Hello");
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::TextMessage);
	CHECK(std::string(reinterpret_cast<const char*>(&decoded[0]), decoded.size()) == "Hello");
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::NoMessage);
	CHECK(decoder.GetNumBytesDecoded() == data.size());
}

TEST_CASE("regressionBug", "[EventLoop Websocket]") {
	// top bit set of second byte of message used to trigger a MASK decode of the remainder
	std::vector<uint8_t> data{
		0x82, 0x05, 0x80, 0x81, 0x82, 0x83, 0x84};
	std::vector<uint8_t> expected_body{0x80, 0x81, 0x82, 0x83, 0x84};
	Common::Websocket::WebsocketDecoder decoder(loop, data);
	std::vector<uint8_t> decoded;
	CHECK(decoder.DecodeMessage(decoded) == Common::Websocket::WebsocketDecoder::MessageState::BinaryMessage);
	CHECK(decoded == expected_body);
	CHECK(decoder.GetNumBytesDecoded() == data.size());
}

TEST_CASE("longStringExamples", "[EventLoop Websocket]") {
	// These are the binary examples, but cast as strings.
	testLongString(256, {0x81, 0x7E, 0x01, 0x00});
	testLongString(65536, {0x81, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});
}

TEST_CASE("pings and pongs", "[EventLoop Websocket]") {
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::Ping, "Hello", {0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});
	testSingleString(Common::Websocket::WebsocketDecoder::MessageState::Pong, "Hello", {0x8a, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f});
}
