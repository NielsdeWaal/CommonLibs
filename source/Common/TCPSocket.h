#ifndef TCP_SOCKET
#define TCP_SOCKET

#include "EventLoop.h"
#include "UringCommands.h"

#include <arpa/inet.h>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <unistd.h>

namespace Common {

class TcpSocket;

class ITcpSocketHandler
{
public:
	ITcpSocketHandler(const ITcpSocketHandler&) = delete;
	ITcpSocketHandler(ITcpSocketHandler&&) = delete;
	ITcpSocketHandler& operator=(const ITcpSocketHandler&) = delete;
	ITcpSocketHandler& operator=(ITcpSocketHandler&&) = delete;

	ITcpSocketHandler() = default;
	virtual ~ITcpSocketHandler() = default;

	virtual void OnConnected() = 0;
	virtual void OnDisconnect([[maybe_unused]] TcpSocket* conn) = 0;
	virtual void OnIncomingData([[maybe_unused]] TcpSocket* conn, char* data, size_t len) = 0;
};

class TcpSocket : public EventLoop::IUringCallbackHandler
{
public:
	TcpSocket(EventLoop::EventLoop& ev, ITcpSocketHandler* handler)
		: mEv(ev)
		, mHandler(handler)
		// , mData(static_cast<char*>(std::aligned_alloc(4096, BUF_SIZE)))
		, mData(new char[BUF_SIZE])
	{
		mLogger = mEv.RegisterLogger("TCPSocket");
		mFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	}

	TcpSocket(EventLoop::EventLoop& ev, int fd, ITcpSocketHandler* handler)
		: mEv(ev)
		, mHandler(handler)
		, mFd(fd)
		// , mData(static_cast<char*>(std::aligned_alloc(4096, BUF_SIZE)))
		, mData(new char[BUF_SIZE])
	{
		mLogger = mEv.RegisterLogger("TCPSocket");
		mLogger->info("Started socket from fd:{}", mFd);
		mConnected = true;
		SubmitRecv();
	}

	~TcpSocket()
	{
		if(mFd)
		{
			// TODO maybe change to async close?
			// Need a NOP callback so we don't get a use-after-free
			// Otherwise we would call the callback even though this object is destroyed
			::close(mFd);
		}
	}

	void Connect(const std::string& addr, const std::uint16_t port) noexcept
	{
		remote.sin_addr.s_addr = ::inet_addr(addr.c_str());
		// remote.sin_addr.s_addr = addr;
		remote.sin_family = AF_INET;
		remote.sin_port = htons(port);

		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

		data->mCallback = static_cast<EventLoop::IUringCallbackHandler*>(this);
		data->mType = EventLoop::SourceType::Connect;
		data->mInfo = EventLoop::CONNECT{.fd = mFd, .addr = (struct sockaddr*)&remote, .len = sizeof(remote)};

		mEv.QueueStandardRequest(std::move(data));
	}

	void Send(char* data, const std::size_t len)
	{
		if(mConnected)
		{
			std::unique_ptr<EventLoop::UserData> usrData = std::make_unique<EventLoop::UserData>();

			usrData->mCallback = static_cast<EventLoop::IUringCallbackHandler*>(this);
			usrData->mType = EventLoop::SourceType::SockSend;
			usrData->mInfo = EventLoop::SOCK_SEND{.fd = mFd, .buf = static_cast<void*>(data), .len = len, .flags = 0};

			mEv.QueueStandardRequest(std::move(usrData));
		}
		else
		{
			mLogger->warn("Attempted send on fd:{}, while not connected", mFd);
		}
	}

	void Shutdown() noexcept
	{
		if(mConnected)
		{
			mConnected = false;
		}
		if(mFd)
		{
			::close(mFd);
			mFd = 0;
		}
	}

	bool IsConnected()
	{
		return mConnected;
	}

private:
	void SubmitRecv()
	{
		// FIXME replace with multi-shot receive - requires kernel 6.0
		mLogger->trace("Queueing recv on fd:{}", mFd);
		std::unique_ptr<EventLoop::UserData> usrData = std::make_unique<EventLoop::UserData>();

		usrData->mCallback = static_cast<EventLoop::IUringCallbackHandler*>(this);
		usrData->mType = EventLoop::SourceType::SockRecv;
		usrData->mInfo =
			EventLoop::SOCK_RECV{.fd = mFd, .buf = static_cast<void*>(mData.get()), .len = BUF_SIZE, .flags = 0};

		mEv.QueueStandardRequest(std::move(usrData));
	}

	void OnCompletion(EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
	{
		switch(data->mType)
		{
		case EventLoop::SourceType::Connect: {
			if(!mConnected)
			{
				mConnected = true;
				mLogger->info("fd:{} connected", mFd);
				mHandler->OnConnected();
				SubmitRecv();
			}
			break;
		}
		case EventLoop::SourceType::SockRecv: {
			mLogger->trace("Received data, len: {}", cqe.res);
			if(cqe.res == 0)
			{
				mLogger->debug("Socket closed");
				mConnected = false;
				mHandler->OnDisconnect(this);
				return;
			}
			else if(cqe.res > 0)
			{
				mHandler->OnIncomingData(this, mData.get(), cqe.res);

				SubmitRecv();
			}
			break;
		}
		case EventLoop::SourceType::SockSend: {
			mLogger->trace("Completion on send");
			break;
		}
		default: {
			mLogger->trace("fd: {}, res: {}", mFd, cqe.res);
			if(cqe.res > 0)
			{
				mLogger->warn("Positive result on unhandled tag");
			}
			else if(cqe.res == 0)
			{
				mLogger->warn("Socket closed");
				mConnected = false;
				mHandler->OnDisconnect(this);
			}
			else if(cqe.res < 0)
			{
				mLogger->error("Socket received error");
			}
			break;
		}
		}
	}

	static constexpr std::size_t BUF_SIZE = 64 * 1024 * 1024; // 128MiB

	EventLoop::EventLoop& mEv;
	ITcpSocketHandler* mHandler;

	bool mConnected{false};
	int mFd{0};
	sockaddr_in remote;

	std::unique_ptr<char[]> mData;

	std::shared_ptr<spdlog::logger> mLogger;
};

class ITcpServerSocketHandler
{
public:
	ITcpServerSocketHandler(const ITcpServerSocketHandler&) = delete;
	ITcpServerSocketHandler(ITcpServerSocketHandler&&) = delete;
	ITcpServerSocketHandler& operator=(const ITcpServerSocketHandler&) = delete;
	ITcpServerSocketHandler& operator=(ITcpServerSocketHandler&&) = delete;

	ITcpServerSocketHandler() = default;
	virtual ~ITcpServerSocketHandler() = default;

	virtual ITcpSocketHandler* OnIncomingConnection() = 0;
};

class TcpSocketServer : public EventLoop::IUringCallbackHandler
{
public:
	TcpSocketServer(EventLoop::EventLoop& ev, ITcpServerSocketHandler* handler)
		: mEv(ev)
		, mHandler(handler)
	{
		mLogger = mEv.RegisterLogger("TcpServer");
		mFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

		int reuseaddrOption = 1;
		if(::setsockopt(mFd, SOL_SOCKET, SO_REUSEADDR, &reuseaddrOption, sizeof(reuseaddrOption)) == -1)
		{
			mLogger->error("Unable to set options on server socket");
			throw std::runtime_error("Unable to set options on server socket");
		}
	}

	void BindAndListen(uint16_t port)
	{
		// const uint16_t port = ::atoi(port);
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = ::htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;

		if(::bind(mFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(sockaddr_in)) == -1)
		{
			mLogger->critical("Unable to bind address to socket");
			throw std::runtime_error("Unable to bind address to socket");
		}

		if(::listen(mFd, 8) == -1)
		{
			mLogger->critical("Unable to open socket for listening");
			throw std::runtime_error("Unable to open socket for listening");
		}

		SubmitAccept();

		mLogger->info("Started TCP server fd:{}, port:{}", mFd, port);
	}

	void Shutdown()
	{
		mLogger->info("Shutting down streamsocket server");
		for(const auto& conn: mConnections)
		{
			conn->Shutdown();
		}
	}

private:
	void SubmitAccept()
	{
		sockaddr sockRemote;
		socklen_t len = sizeof(sockaddr_in);
		// FIXME replace with multi-shot accept
		std::unique_ptr<EventLoop::UserData> usrData = std::make_unique<EventLoop::UserData>();

		usrData->mCallback = this;
		usrData->mType = mUseMultishotAccept ? EventLoop::SourceType::MultiShotAccept : EventLoop::SourceType::Accept;
		usrData->mInfo = EventLoop::ACCEPT{.fd = mFd, .addr = &sockRemote, .len = &len, .flags = 0};
		usrData->mReqType = mUseMultishotAccept ? EventLoop::RequestType::MultiShot : EventLoop::RequestType::Normal;

		mEv.QueueStandardRequest(std::move(usrData));
	}

	void OnCompletion(EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
	{
		switch(data->mType)
		{
		case EventLoop::SourceType::MultiShotAccept:
			[[fallthrough]];
		case EventLoop::SourceType::Accept: {
			if(cqe.res < 0)
			{
				mLogger->error("Error accepting connection, fd:{}, errno:{}", mFd, cqe.res);
			}
			else
			{
				ITcpSocketHandler* connHandler = mHandler->OnIncomingConnection();
				if(connHandler != nullptr)
				{
					mConnections.emplace_back(std::make_unique<TcpSocket>(mEv, cqe.res, connHandler));
					// mConnections.push_back(new TcpSocket(mEv, cqe.res, connHandler));
				}

				if(!mUseMultishotAccept)
				{
					SubmitAccept();
				}
			}
			break;
		}
		default: {
			mLogger->error("Unhandled event type");
			assert(false);
			break;
		}
		}
	}

	static constexpr std::size_t BUF_SIZE = 128 * 1024 * 1024;

	EventLoop::EventLoop& mEv;
	ITcpServerSocketHandler* mHandler;

	int mFd{0};

	bool mUseMultishotAccept{true};

	std::vector<std::unique_ptr<TcpSocket>>
		mConnections; // TODO This is dumb, user can not access the actual connection
	// std::vector<TcpSocket*> mConnections;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace Common

#endif