#include <catch2/catch.hpp>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <csignal>

#include "EventLoop.h"
#include "StreamSocket.h"


TEST_CASE("TCP Read And Write", "[Eventloop TCP]")
{
	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	const std::string localAddress{"127.0.0.1"};
	const std::uint16_t localPort = 1337;

	struct Server : public Common::IStreamSocketServerHandler
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

		void OnDisconnect(Common::StreamSocket* conn) final
		{}

		void OnIncomingData(Common::StreamSocket* conn, char* data, std::size_t len) final
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
			auto dataWrite = std::unique_ptr<char[]>(new char[3]{'a','b','c'});
			mSocket.Send(dataWrite.get(), 3);
		}

		void OnDisconnect(Common::StreamSocket* conn) final
		{
			REQUIRE(conn->IsConnected() == false);
			std::raise(SIGINT);
		}

		void OnIncomingData(Common::StreamSocket* conn, char* data, std::size_t len) final
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

	struct Server : public Common::IStreamSocketServerHandler
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

		void OnDisconnect(Common::StreamSocket* conn) final
		{}

		void OnIncomingData(Common::StreamSocket* conn, char* data, std::size_t len) final
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
			const int addr = mSocket.GetPeerAddress();
			const std::uint16_t port = mSocket.GetPeerPort();
			REQUIRE(addr == ::inet_addr(mHostname.data()));
			REQUIRE(port == mLocalport);

			auto dataWrite = std::unique_ptr<char[]>(new char[3]{'a'});
			mSocket.Send(dataWrite.get(), 1);
		}

		void OnDisconnect(Common::StreamSocket* conn) final
		{
			REQUIRE(conn->IsConnected() == false);
			std::raise(SIGINT);
		}

		void OnIncomingData(Common::StreamSocket* conn, char* data, std::size_t len) final
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
