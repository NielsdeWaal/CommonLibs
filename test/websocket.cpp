#include <arpa/inet.h>
#include <bits/c++config.h>
#include <catch2/catch.hpp>
#include <csignal>
#include <memory>
#include <netinet/in.h>
#include <shared_mutex>
#include <sys/socket.h>

#include "EventLoop.h"
#include "Websocket.h"

/**
 * @brief testcase for websocket connection
 * TODO Implement client websocket encoding, and serverside
 */

TEST_CASE("Basics Bits", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x00, 0x00); // All False
	Common::Websocket::WebsocketDecoder::BasicHeader h2(0xF0, 0x80); // All True

	REQUIRE(Common::Websocket::WebsocketDecoder::GetFin(h1) == false);

	REQUIRE(Common::Websocket::WebsocketDecoder::GetFin(h2) == true);
}

TEST_CASE("Basics Size", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x00, 0x00); // Length 0
	Common::Websocket::WebsocketDecoder::BasicHeader h2(0x00, 0x01); // Length 1
	Common::Websocket::WebsocketDecoder::BasicHeader h3(0x00, 0x7D); // Length 125
	Common::Websocket::WebsocketDecoder::BasicHeader h4(0x00, 0x7E); // Length 126
	Common::Websocket::WebsocketDecoder::BasicHeader h5(0x00, 0x7F); // Length 127
	Common::Websocket::WebsocketDecoder::BasicHeader h6(0x00, 0x80); // Length 0, mask bit set

	REQUIRE(Common::Websocket::WebsocketDecoder::GetBasicSize(h1) == 0);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetBasicSize(h2) == 1);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetBasicSize(h3) == 125);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetBasicSize(h4) == 126);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetBasicSize(h5) == 127);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetBasicSize(h6) == 0);
}

TEST_CASE("Basics Header Length", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x82, 0x00); // short binary frame, unmasked
	Common::Websocket::WebsocketDecoder::BasicHeader h2(0x82, 0x80); // short binary frame, masked
	Common::Websocket::WebsocketDecoder::BasicHeader h3(0x82, 0x7E); // medium binary frame, unmasked
	Common::Websocket::WebsocketDecoder::BasicHeader h4(0x82, 0xFE); // medium binary frame, masked
	Common::Websocket::WebsocketDecoder::BasicHeader h5(0x82, 0x7F); // jumbo binary frame, unmasked
	Common::Websocket::WebsocketDecoder::BasicHeader h6(0x82, 0xFF); // jumbo binary frame, masked

	REQUIRE(Common::Websocket::WebsocketDecoder::GetHeaderLength(h1) == 2);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetHeaderLength(h2) == 6);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetHeaderLength(h3) == 4);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetHeaderLength(h4) == 8);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetHeaderLength(h5) == 10);
	REQUIRE(Common::Websocket::WebsocketDecoder::GetHeaderLength(h6) == 14);
}

TEST_CASE("Basics Opcodes", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x00, 0x00);

	REQUIRE(Common::Websocket::WebsocketDecoder::IsControl(Common::Websocket::WebsocketDecoder::Opcode::CONTINUATION) ==
			false);
	REQUIRE(Common::Websocket::WebsocketDecoder::IsControl(Common::Websocket::WebsocketDecoder::Opcode::TEXT) == false);
	REQUIRE(
		Common::Websocket::WebsocketDecoder::IsControl(Common::Websocket::WebsocketDecoder::Opcode::BINARY) == false);
	REQUIRE(Common::Websocket::WebsocketDecoder::IsControl(Common::Websocket::WebsocketDecoder::Opcode::CLOSE) == true);
	REQUIRE(Common::Websocket::WebsocketDecoder::IsControl(Common::Websocket::WebsocketDecoder::Opcode::PING) == true);
	REQUIRE(Common::Websocket::WebsocketDecoder::IsControl(Common::Websocket::WebsocketDecoder::Opcode::PONG) == true);

	REQUIRE(Common::Websocket::WebsocketDecoder::GetOpcode(h1) ==
			Common::Websocket::WebsocketDecoder::Opcode::CONTINUATION);
}

TEST_CASE("ExtendedHeader Basics", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::ExtendedHeader h1;
	std::uint8_t h1_solution[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	Common::Websocket::WebsocketDecoder::ExtendedHeader h2(std::uint16_t(255));
	std::uint8_t h2_solution[12] = {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	Common::Websocket::WebsocketDecoder::ExtendedHeader h3(std::uint16_t(256), htonl(0x8040201));
	std::uint8_t h3_solution[12] = {0x01, 0x00, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	Common::Websocket::WebsocketDecoder::ExtendedHeader h4(std::uint64_t(0x0807060504030201LL));
	std::uint8_t h4_solution[12] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00};

	Common::Websocket::WebsocketDecoder::ExtendedHeader h5(std::uint64_t(0x0807060504030201LL), htonl(0x8040201));
	std::uint8_t h5_solution[12] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x08, 0x04, 0x02, 0x01};

	CHECK(std::equal(h1_solution, h1_solution + 12, h1.mBytes));
	CHECK(std::equal(h2_solution, h2_solution + 12, h2.mBytes));
	CHECK(std::equal(h3_solution, h3_solution + 12, h3.mBytes));
	CHECK(std::equal(h4_solution, h4_solution + 12, h4.mBytes));
	CHECK(std::equal(h5_solution, h5_solution + 12, h5.mBytes));
}

TEST_CASE("ExtendedHeader property extractors", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x00, 0x7E);
	Common::Websocket::WebsocketDecoder::ExtendedHeader e1(std::uint16_t(255));
	CHECK(Common::Websocket::WebsocketDecoder::GetExtendedSize(e1) == 255);
	CHECK(Common::Websocket::WebsocketDecoder::GetPayloadSize(h1, e1) == 255);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKeyOffset(h1) == 2);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKey(h1, e1).i == 0);

	Common::Websocket::WebsocketDecoder::BasicHeader h2(0x00, 0x7F);
	Common::Websocket::WebsocketDecoder::ExtendedHeader e2(std::uint64_t(0x0807060504030201LL));
	CHECK(Common::Websocket::WebsocketDecoder::GetJumboSize(e2) == 0x0807060504030201LL);
	CHECK(Common::Websocket::WebsocketDecoder::GetPayloadSize(h2, e2) == 0x0807060504030201LL);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKeyOffset(h2) == 8);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKey(h2, e2).i == 0);

	Common::Websocket::WebsocketDecoder::BasicHeader h3(0x00, 0xFE);
	Common::Websocket::WebsocketDecoder::ExtendedHeader e3(std::uint16_t(255), 0x08040201);
	CHECK(Common::Websocket::WebsocketDecoder::GetExtendedSize(e3) == 255);
	CHECK(Common::Websocket::WebsocketDecoder::GetPayloadSize(h3, e3) == 255);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKeyOffset(h3) == 2);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKey(h3, e3).i == 0x08040201);

	Common::Websocket::WebsocketDecoder::BasicHeader h4(0x00, 0xFF);
	Common::Websocket::WebsocketDecoder::ExtendedHeader e4(std::uint64_t(0x0807060504030201LL), 0x08040201);
	CHECK(Common::Websocket::WebsocketDecoder::GetJumboSize(e4) == 0x0807060504030201LL);
	CHECK(Common::Websocket::WebsocketDecoder::GetPayloadSize(h4, e4) == 0x0807060504030201LL);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKeyOffset(h4) == 8);
	CHECK(Common::Websocket::WebsocketDecoder::GetMaskingKey(h4, e4).i == 0x08040201);

	Common::Websocket::WebsocketDecoder::BasicHeader h5(0x00, 0x7D);
	Common::Websocket::WebsocketDecoder::ExtendedHeader e5;
	CHECK(Common::Websocket::WebsocketDecoder::GetPayloadSize(h5, e5) == 125);
}

TEST_CASE("Header preparation", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x81, 0xFF); //
	Common::Websocket::WebsocketDecoder::ExtendedHeader e1(uint64_t(0xFFFFFLL), htonl(0xD5FB70EE));
	std::string p1 = Common::Websocket::WebsocketDecoder::PrepareHeader(h1, e1);
	uint8_t s1[14] = {0x81, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xD5, 0xFB, 0x70, 0xEE};

	CHECK(p1.size() == 14);
	CHECK(std::equal(p1.begin(), p1.end(), reinterpret_cast<char*>(s1)));

	Common::Websocket::WebsocketDecoder::BasicHeader h2(0x81, 0x7E); //
	Common::Websocket::WebsocketDecoder::ExtendedHeader e2(uint16_t(255));
	std::string p2 = Common::Websocket::WebsocketDecoder::PrepareHeader(h2, e2);
	uint8_t s2[4] = {0x81, 0x7E, 0x00, 0xFF};

	CHECK(p2.size() == 4);
	CHECK(std::equal(p2.begin(), p2.end(), reinterpret_cast<char*>(s2)));
}

TEST_CASE("Prepare masking key 1", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::uint32_converter key;

	key.i = htonl(0x12345678);
	if(sizeof(std::size_t) == 8)
	{
		CHECK(Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key) ==
			  Common::Websocket::WebsocketDecoder::_htonll(0x1234567812345678LL));
	}
	else
	{
		CHECK(Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key) == htonl(0x12345678));
	}
}

TEST_CASE("Prepare masking key 2", "[EventLoop Websocket]")
{
	Common::Websocket::WebsocketDecoder::uint32_converter key;

	key.i = htonl(0xD5FB70EE);
	if(sizeof(std::size_t) == 8)
	{
		CHECK(Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key) ==
			  Common::Websocket::WebsocketDecoder::_htonll(0xD5FB70EED5FB70EELL));
	}
	else
	{
		CHECK(Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key) == htonl(0xD5FB70EE));
	}
}

TEST_CASE("Block byte mask", "[EventLoop Websocket]")
{
	std::uint8_t input[15] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	std::uint8_t output[15];

	std::uint8_t masked[15] = {
		0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02};

	Common::Websocket::WebsocketDecoder::uint32_converter key;
	key.c[0] = 0x00;
	key.c[1] = 0x01;
	key.c[2] = 0x02;
	key.c[3] = 0x03;

	Common::Websocket::WebsocketDecoder::ByteMask(input, input + 15, output, key);

	CHECK(std::equal(output, output + 15, masked));
}

TEST_CASE("Block byte mask in place", "[EventLoop Websocket]")
{
	std::uint8_t buffer[15] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	std::uint8_t masked[15] = {
		0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02};

	Common::Websocket::WebsocketDecoder::uint32_converter key;
	key.c[0] = 0x00;
	key.c[1] = 0x01;
	key.c[2] = 0x02;
	key.c[3] = 0x03;

	Common::Websocket::WebsocketDecoder::ByteMask(buffer, buffer + 15, key);

	CHECK(std::equal(buffer, buffer + 15, masked));
}

TEST_CASE("Continuous byte mask", "[EventLoop Websocket]")
{
	std::uint8_t input[16];
	std::uint8_t output[16];

	std::uint8_t masked[16] = {
		0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x00};

	Common::Websocket::WebsocketDecoder::uint32_converter key;
	key.c[0] = 0x00;
	key.c[1] = 0x01;
	key.c[2] = 0x02;
	key.c[3] = 0x03;

	// One call
	std::size_t pkey = 0;
	std::size_t pkey_temp = 0;
	pkey = Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key);
	std::fill_n(input, 16, 0x00);
	std::fill_n(output, 16, 0x00);
	Common::Websocket::WebsocketDecoder::ByteMaskCirc(input, output, 15, pkey);
	CHECK(std::equal(output, output + 16, masked));

	// calls not split on word boundaries
	pkey = Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key);
	std::fill_n(input, 16, 0x00);
	std::fill_n(output, 16, 0x00);

	pkey_temp = Common::Websocket::WebsocketDecoder::ByteMaskCirc(input, output, 7, pkey);
	CHECK(std::equal(output, output + 7, masked));
	CHECK(pkey_temp == Common::Websocket::WebsocketDecoder::CircshiftPrepKey(pkey, 3));

	pkey_temp = Common::Websocket::WebsocketDecoder::ByteMaskCirc(input + 7, output + 7, 8, pkey_temp);
	CHECK(std::equal(output, output + 16, masked));
	REQUIRE(pkey_temp == Common::Websocket::WebsocketDecoder::CircshiftPrepKey(pkey, 3));
}

TEST_CASE("Continuous byte mask inplace", "[EventLoop Websocket]")
{
	std::uint8_t buffer[16];

	std::uint8_t masked[16] = {
		0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x00};

	Common::Websocket::WebsocketDecoder::uint32_converter key;
	key.c[0] = 0x00;
	key.c[1] = 0x01;
	key.c[2] = 0x02;
	key.c[3] = 0x03;

	// One call
	std::size_t pkey = 0;
	std::size_t pkey_temp = 0;
	pkey = Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key);
	std::fill_n(buffer, 16, 0x00);
	Common::Websocket::WebsocketDecoder::ByteMaskCirc(buffer, 15, pkey);
	CHECK(std::equal(buffer, buffer + 16, masked));

	// calls not split on word boundaries
	pkey = Common::Websocket::WebsocketDecoder::PrepareMaskingKey(key);
	std::fill_n(buffer, 16, 0x00);

	pkey_temp = Common::Websocket::WebsocketDecoder::ByteMaskCirc(buffer, 7, pkey);
	CHECK(std::equal(buffer, buffer + 7, masked));
	CHECK(pkey_temp == Common::Websocket::WebsocketDecoder::CircshiftPrepKey(pkey, 3));

	pkey_temp = Common::Websocket::WebsocketDecoder::ByteMaskCirc(buffer + 7, 8, pkey_temp);
	CHECK(std::equal(buffer, buffer + 16, masked));
	CHECK(pkey_temp == Common::Websocket::WebsocketDecoder::CircshiftPrepKey(pkey, 3));
}

TEST_CASE("Empty binary frame unmasked", "[EventLoop Websocket]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();
	std::uint8_t frame[2] = {0x82, 0x00};

	Common::Websocket::WebsocketDecoder::BasicHeader h1(0x82, 0x00);

	REQUIRE(Common::Websocket::WebsocketDecoder::GetFin(h1) == true);

	// all in one chunk
	Common::Websocket::WebsocketDecoder env1(loop);

	std::size_t ret1 = env1.ConsumeMessage(frame, 2);

	CHECK(ret1 == 2);
	CHECK(env1.Ready());

	// two separate chunks
	Common::Websocket::WebsocketDecoder env2(loop);

	CHECK(env2.ConsumeMessage(frame, 1) == 1);
	CHECK_FALSE(env2.Ready());

	CHECK(env2.ConsumeMessage(frame + 1, 1) == 1);
	CHECK(env2.Ready());
}

TEST_CASE("Small binary fram unmasked", "[EventLoop Websocket]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	std::uint8_t frame[4] = {0x82, 0x02, 0x2A, 0x2A};

	Common::Websocket::WebsocketDecoder env(loop);

	CHECK(env.GetMessage() == nullptr);
	CHECK(env.ConsumeMessage(frame, 4) == 4);
	CHECK(env.Ready() == true);

	std::unique_ptr<Common::Websocket::WebsocketDecoder::Message> foo = env.GetMessage();

	CHECK(env.GetMessage() == nullptr);
	CHECK(foo->GetPayload().size() == 2);
	CHECK(foo->GetPayload() == "**");
}

TEST_CASE("Frame extended binary unmasked", "[EventLoop Websocket]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Common::Websocket::WebsocketDecoder env(loop);

	std::uint8_t frame[130] = {0x82, 0x7E, 0x00, 0x7E};
	frame[0] = 0x82;
	frame[1] = 0x7E;
	frame[2] = 0x00;
	frame[3] = 0x7E;
	std::fill_n(frame + 4, 126, 0x2A);

	CHECK(env.GetMessage() == nullptr);
	CHECK(env.ConsumeMessage(frame, 130) == 130);
	CHECK(env.Ready() == true);

	std::unique_ptr<Common::Websocket::WebsocketDecoder::Message> foo = env.GetMessage();

	CHECK(env.GetMessage() == nullptr);
	CHECK(foo->GetPayload().size() == 126);
}

TEST_CASE("Frame jumbo binary unmasked", "[EventLoop Websocket]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Common::Websocket::WebsocketDecoder env(loop);

	std::uint8_t frame[130] = {0x82, 0x7E, 0x00, 0x7E};
	std::fill_n(frame + 4, 126, 0x2A);

	CHECK(env.GetMessage() == nullptr);
	CHECK(env.ConsumeMessage(frame, 130) == 130);
	CHECK(env.Ready() == true);

	std::unique_ptr<Common::Websocket::WebsocketDecoder::Message> foo = env.GetMessage();

	CHECK(env.GetMessage() == nullptr);
	CHECK(foo->GetPayload().size() == 126);
}

TEST_CASE("Fragmented binary message", "[EventLoop Websocket]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Common::Websocket::WebsocketDecoder env0(loop);
	Common::Websocket::WebsocketDecoder env1(loop);

	std::uint8_t frame0[6] = {0x02, 0x01, 0x2A, 0x80, 0x01, 0x2A};
	std::uint8_t frame1[8] = {0x02, 0x01, 0x2A, 0x89, 0x00, 0x80, 0x01, 0x2A};

	// read fragmented message in one chunk
	CHECK(env0.GetMessage() == nullptr);
	CHECK(env0.ConsumeMessage(frame0, 6) == 6);
	CHECK(env0.Ready() == true);
	std::unique_ptr<Common::Websocket::WebsocketDecoder::Message> foo = env0.GetMessage();
	CHECK(foo->GetPayload().size() == 2);
	CHECK(foo->GetPayload() == "**");

	// read fragmented message in two chunks
	CHECK(env0.GetMessage() == nullptr);
	CHECK(env0.ConsumeMessage(frame0, 3) == 3);
	CHECK(env0.Ready() == false);
	CHECK(env0.ConsumeMessage(frame0 + 3, 3) == 3);
	CHECK(env0.Ready() == true);
	foo = env0.GetMessage();
	CHECK(foo->GetPayload().size() == 2);
	CHECK(foo->GetPayload() == "**");

	// read fragmented message with control message in between
	CHECK(env0.GetMessage() == nullptr);
	CHECK(env0.ConsumeMessage(frame1, 8) == 5);
	CHECK(env0.Ready() == true);
	CHECK(env0.GetMessage()->GetOpcode() == Common::Websocket::WebsocketDecoder::Opcode::PING);
	CHECK(env0.ConsumeMessage(frame1 + 5, 3) == 3);
	CHECK(env0.Ready() == true);
	foo = env0.GetMessage();
	CHECK(foo->GetPayload().size() == 2);
	CHECK(foo->GetPayload() == "**");

	// read lone continuation frame
	// CHECK(env0.p.get_message() == nullptr);
	// CHECK_GT(env0.consume(frame0 + 3, 3, env0.ec), 0);
	// CHECK_EQUAL(env0.ec, websocketpp::processor::error::invalid_continuation);

	// read two start frames in a row
	// CHECK_EQUAL(env1.get_message() == nullptr);
	// CHECK_EQUAL(env1.consume(frame0, 3, env1.ec), 3);
	// CHECK_GT(env1.consume(frame0, 3, env1.ec), 0);
	// CHECK_EQUAL(env1.ec, websocketpp::processor::error::invalid_continuation);
}

TEST_CASE("Fragmented binary masked message", "[EventLoop Websocket]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Common::Websocket::WebsocketDecoder env0(loop);

	std::uint8_t frame0[14] = {0x02, 0x81, 0xAB, 0x23, 0x98, 0x45, 0x81, 0x80, 0x81, 0xB8, 0x34, 0x12, 0xFF, 0x92};

	// read fragmented message in one chunk
	CHECK(env0.GetMessage() == nullptr);
	CHECK(env0.ConsumeMessage(frame0, 14) == 14);
	CHECK(env0.Ready() == true);
	CHECK(env0.GetMessage()->GetPayload() == "**");
}
