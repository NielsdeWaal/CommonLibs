#include <catch2/catch.hpp>

#include <arpa/inet.h>
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>

#include <csignal>

#include "EventLoop.h"
#include "StreamSocket.h"
#include "TCPSocket.h"

TEST_CASE("TCP Read And Write", "[Eventloop TCP]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	const std::string localAddress{"127.0.0.1"};
	const std::uint16_t localPort = 1337;

	struct Server
		: public Common::IStreamSocketServerHandler
		, public Common::IStreamSocketHandler
	{
		Server(EventLoop::EventLoop& ev, std::uint16_t bindPort)
			: mEv(ev)
			, mSocket(mEv, this)
		{
			mSocket.BindAndListen(bindPort);
		}

		IStreamSocketHandler* OnIncomingConnection() final
		{
			return this;
		}

		void OnConnected() final
		{}

		void OnDisconnect([[maybe_unused]] Common::StreamSocket* conn) final
		{}

		void OnIncomingData([[maybe_unused]] Common::StreamSocket* conn, char* data, std::size_t len) final
		{
			REQUIRE(conn->IsConnected());
			REQUIRE(std::string{data, len} == "abc");
			conn->Shutdown();
		}

	private:
		EventLoop::EventLoop& mEv;
		Common::StreamSocketServer mSocket;
	};

	struct Client : public Common::IStreamSocketHandler
	{
		Client(EventLoop::EventLoop& ev, std::string hostname, std::uint16_t localPort)
			: mEv(ev)
			, mSocket(mEv, this)
		{
			mSocket.Connect(hostname.data(), localPort);
		}

		void OnConnected() final
		{
			auto dataWrite = std::unique_ptr<char[]>(new char[3]{'a', 'b', 'c'});
			mSocket.Send(dataWrite.get(), 3);
		}

		void OnDisconnect([[maybe_unused]] Common::StreamSocket* conn) final
		{
			REQUIRE(conn->IsConnected() == false);
			std::raise(SIGINT);
		}

		void OnIncomingData([[maybe_unused]] Common::StreamSocket* conn, [[maybe_unused]] char* data,
			[[maybe_unused]] std::size_t len) final
		{}

	private:
		EventLoop::EventLoop& mEv;
		Common::StreamSocket mSocket;
	};

	Server server(loop, localPort);
	Client client(loop, localAddress, localPort);

	loop.Run();
}

TEST_CASE("TCP PeerId", "[Eventloop TCP]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	const std::string localAddress{"127.0.0.1"};
	const std::uint16_t localPort = 1337;

	struct Server
		: public Common::IStreamSocketServerHandler
		, public Common::IStreamSocketHandler
	{
		Server(EventLoop::EventLoop& ev, std::uint16_t bindPort)
			: mEv(ev)
			, mSocket(mEv, this)
		{
			mSocket.BindAndListen(bindPort);
		}

		IStreamSocketHandler* OnIncomingConnection() final
		{
			return this;
		}

		void OnConnected() final
		{}

		void OnDisconnect([[maybe_unused]] Common::StreamSocket* conn) final
		{}

		void OnIncomingData([[maybe_unused]] Common::StreamSocket* conn, [[maybe_unused]] char* data,
			[[maybe_unused]] std::size_t len) final
		{
			conn->Shutdown();
		}

	private:
		EventLoop::EventLoop& mEv;
		Common::StreamSocketServer mSocket;
	};

	struct Client : public Common::IStreamSocketHandler
	{
		Client(EventLoop::EventLoop& ev, std::string hostname, std::uint16_t localPort)
			: mEv(ev)
			, mSocket(mEv, this)
			, mHostname(hostname)
			, mLocalport(localPort)
		{
			mSocket.Connect(hostname.data(), localPort);
		}

		void OnConnected() final
		{
			const std::uint32_t addr = mSocket.GetPeerAddress();
			const std::uint16_t port = mSocket.GetPeerPort();
			REQUIRE(addr == ::inet_addr(mHostname.data()));
			REQUIRE(port == mLocalport);

			auto dataWrite = std::unique_ptr<char[]>(new char[3]{'a'});
			mSocket.Send(dataWrite.get(), 1);
		}

		void OnDisconnect([[maybe_unused]] Common::StreamSocket* conn) final
		{
			REQUIRE(conn->IsConnected() == false);
			std::raise(SIGINT);
		}

		void OnIncomingData([[maybe_unused]] Common::StreamSocket* conn, [[maybe_unused]] char* data,
			[[maybe_unused]] std::size_t len) final
		{}

	private:
		EventLoop::EventLoop& mEv;
		Common::StreamSocket mSocket;
		std::string mHostname;
		std::uint16_t mLocalport;
	};

	Server server(loop, localPort);
	Client client(loop, localAddress, localPort);

	loop.Run();
}

TEST_CASE("Uring TCP Read And Write", "[Eventloop TCP]")
{
	std::vector<std::vector<char>> buffers;
	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_int_distribution<std::mt19937::result_type> dist(1, 255);

	buffers.resize(5);
	for(auto& buf: buffers)
	{
		buf.resize(100);
		std::generate(buf.begin(), buf.end(), [&] {
			return dist(rng);
		});
	}

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	const std::string localAddress{"127.0.0.1"};
	const std::uint16_t localPort = 1337;

	struct Server
		: public Common::ITcpServerSocketHandler
		, public Common::ITcpSocketHandler
	{
		Server(EventLoop::EventLoop& ev, std::uint16_t bindPort, std::vector<std::vector<char>>& data)
			: mEv(ev)
			, mSocket(mEv, this)
			, mData(data)
		{
			mSocket.BindAndListen(bindPort);
		}

		Common::ITcpSocketHandler* OnIncomingConnection() final
		{
			spdlog::info("Server port incoming connection");
			return this;
		}

		void OnConnected() final
		{
			spdlog::info("Server port connected");
		}

		void OnDisconnect(Common::TcpSocket* conn) final
		{
			REQUIRE(conn->IsConnected() == false);
			std::raise(SIGINT);
		}

		void OnIncomingData(Common::TcpSocket* conn, char* data, std::size_t len) final
		{
			spdlog::info("Server received data, len:{}", len);
			REQUIRE(conn->IsConnected());
			conn->Send(data, len);
		}

	private:
		EventLoop::EventLoop& mEv;
		Common::TcpSocketServer mSocket;
		std::vector<std::vector<char>>& mData;
		int mOffset{0};
	};

	struct Client : public Common::ITcpSocketHandler
	{
		Client(EventLoop::EventLoop& ev, std::string hostname, std::uint16_t localPort,
			std::vector<std::vector<char>>& data)
			: mEv(ev)
			, mSocket(mEv, this)
			, mData(data)
		{
			mSocket.Connect(hostname.data(), localPort);
		}

		void OnConnected() final
		{
			spdlog::info("Client connected");
			mSocket.Send(mData.at(mOffset).data(), mData.at(mOffset).size());
		}

		void OnDisconnect(Common::TcpSocket* conn) final
		{
			spdlog::info("Client disconnected");
			REQUIRE(conn->IsConnected() == false);
			std::raise(SIGINT);
		}

		void OnIncomingData(Common::TcpSocket* conn, char* data, std::size_t len) final
		{
			spdlog::info("Client incoming data, len: {}, offset: {}", len, mOffset);
			REQUIRE(conn->IsConnected());
			REQUIRE(std::string{data, len} == std::string{mData.at(mOffset).data(), mData.at(mOffset).size()});

			++mOffset;
			if(mOffset > 4 && !mCompleted) {
				spdlog::info("Socket complete");
				conn->Shutdown();
				mCompleted = true;
				return;
			}

			spdlog::info("Client send offset: {}", mOffset);
			conn->Send(mData.at(mOffset).data(), mData.at(mOffset).size());
		}

	private:
		EventLoop::EventLoop& mEv;
		Common::TcpSocket mSocket;
		std::vector<std::vector<char>>& mData;
		int mOffset{0};
		bool mCompleted{false};
	};

	Server server(loop, localPort, buffers);
	Client client(loop, localAddress, localPort, buffers);

	loop.Run();
}
