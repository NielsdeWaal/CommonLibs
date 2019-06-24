#ifndef STREAMSOCKET_H
#define STREAMSOCKET_H

#include "EventLoop.h"

namespace Common {

using namespace EventLoop;

class StreamSocket;

class IStreamSocketHandler
{
public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect() = 0;
	virtual void OnIncomingData(StreamSocket* conn, char* data, size_t len) = 0;
	virtual ~IStreamSocketHandler() {}
};

class StreamSocket : public EventLoop::IFiledescriptorCallbackHandler
{
public:
	//StreamSocket(uint32_t addr, uint16_t port)
	//	: mAddress(addr)
	//	, mPort(port)
	//{}
	
	StreamSocket(EventLoop::EventLoop& ev, IStreamSocketHandler* handler) noexcept
		: mEventLoop(ev)
		, mHandler(handler)
	{
		mFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		//mFd = socket(AF_INET, SOCK_STREAM, 0);
		//if(mFd == -1)
		//{
		//	spdlog::critical("Socket error");
		//	throw std::runtime_error("Socket error");
		//}

		//int flags = ::fcntl(mFd, F_GETFL, 0);
		//if (flags < 0)
		//{
		//	throw std::runtime_error("fcntl(F_GETFL) failed.");
		//}

		//int res = ::fcntl(mFd, F_SETFL, flags|O_NONBLOCK);
		//if (res == -1)
		//{
		//	throw std::runtime_error("fcntl(F_SETFL, flags|O_NONBLOCK) failed.");
		//}
	}

	StreamSocket(EventLoop::EventLoop& ev, int fd, IStreamSocketHandler* handler)
		: mEventLoop(ev)
		, mHandler(handler)
		, mFd(fd)
	{
		mEventLoop.RegisterFiledescriptor(fd, EPOLLIN, this);
		mConnected = true;
	}

	~StreamSocket()
	{
		if(mConnected)
		{
			mEventLoop.UnregisterFiledescriptor(mFd);
		}
		if(mFd)
		{
			::close(mFd);
		}
	}

	//void Connect(uint32_t addr, uint16_t port)
	void Connect(const char* addr, const uint16_t port)
	{
		mPort = port;

		remote.sin_addr.s_addr = ::inet_addr(addr);
		//remote.sin_addr.s_addr = addr;
		remote.sin_family = AF_INET;
		remote.sin_port = htons(mPort);

		auto ret = ::connect(mFd, (struct sockaddr *)&remote, sizeof(struct sockaddr));

		if((ret == -1) && (errno == EINPROGRESS))
		{
			//spdlog::critical("Connect failed, code:{}", ret);
			//throw std::runtime_error("Connect failed");
			mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN | EPOLLOUT, this);
		}
		else
		{
			mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN, this);
			spdlog::info("fd:{} connected instantly", mFd);
		}
	}

	void Send(const char* data, const size_t len) noexcept
	{
		if(mConnected)
		{
			::send(mFd, data, len, MSG_DONTWAIT);
			mSendInProgress = true;
		}
		else
		{
			spdlog::warn("Attempted send on fd:{}, while not connected", mFd);
		}
	}

	bool IsConnected() noexcept
	{
		return mConnected;
	}

private:

	void OnFiledescriptorWrite(int fd) final
	{
		if(!mConnected)
		{
			int err = 0;
			socklen_t len = sizeof(int);
			int status = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
			if (status != -1)
			{
				if (err == 0)
				{
					mEventLoop.ModifyFiledescriptor(fd, EPOLLIN, this);
					mConnected = true;
					mHandler->OnConnected();
				}
				else
				{
					mEventLoop.UnregisterFiledescriptor(mFd);
					mHandler->OnDisconnect();
					mConnected = false;
				}
			}
		}
		else if(mSendInProgress)
		{
			
		}
		else //TODO Make check for connection after sending data i.e check for ack
		{
			//spdlog::info("Connection has shutdown, closing socket");
			//::close(mFd);
			//mEventLoop.UnregisterFiledescriptor(mFd);
			//mConnected = false;
		}
	}

	void OnFiledescriptorRead(int fd) final
	{
		std::array<char, 512> readBuf = {0};
		const auto len = ::recv(fd, readBuf.data(), sizeof(readBuf), MSG_DONTWAIT);

		mHandler->OnIncomingData(this, readBuf.data(), len);
	}

private:
	EventLoop::EventLoop& mEventLoop;
	IStreamSocketHandler* mHandler;

	int mFd = 0;
	struct sockaddr_in remote;

	uint32_t mAddress = 0;
	uint16_t mPort = 0;

	bool mConnected = false;
	bool mSendInProgress = false;

};

class IStreamSocketServerHandler
{
public:
	virtual IStreamSocketHandler* OnIncomingConnection() = 0;
	virtual ~IStreamSocketServerHandler() {}
};

class StreamSocketServer : public EventLoop::IFiledescriptorCallbackHandler
{
public:
	StreamSocketServer(EventLoop::EventLoop& ev, IStreamSocketServerHandler* handler)
		: mEventLoop(ev)
		, mHandler(handler)
	{
		mFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

		int reuseaddrOption = 1;
		if (::setsockopt(mFd, SOL_SOCKET, SO_REUSEADDR, &reuseaddrOption, sizeof(reuseaddrOption)) == -1 ) {
			spdlog::critical("Unable to set options on server socket");
			throw std::runtime_error("Unable to set options on server socket");
		}
	}

	void BindAndListen(uint16_t port)
	{
		//const uint16_t port = ::atoi(port);
		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = ::htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;

		if(::bind(mFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(sockaddr_in)) == -1) {
			spdlog::critical("Unable to bind address to socket");
			throw std::runtime_error("Unable to bind address to socket");
		}

		if(::listen(mFd, 8) == -1)
		{
			spdlog::critical("Unable to open socket for listening");
			throw std::runtime_error("Unable to open socket for listening");
		}

		mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN, this);

		spdlog::info("Started TCP server fd:{}, port:{}", mFd, port);
	}

private:
	void OnFiledescriptorRead(int fd) final
	{
		sockaddr_in remote;
		socklen_t len = sizeof(sockaddr_in);
		int ret = ::accept(fd, reinterpret_cast<sockaddr *>(&remote), &len);
		if (ret == -1)
		{
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
				spdlog::warn("Incoming connection already closed");
			}
			else
			{
				spdlog::critical("Unhandled error on incoming connection, fd:{}, errno:{}", fd, errno);
				throw std::runtime_error("Unhandled error on incoming connection");
			}
		}
		else
		{
			auto connHandler = mHandler->OnIncomingConnection();
			if(connHandler != nullptr)
			{
				//mConnections.push_back(StreamSocket(mEventLoop, ret, connHandler));
				mConnections.push_back(std::make_unique<StreamSocket>(mEventLoop, ret, connHandler));
			}
			else
			{
				spdlog::info("Incoming connection rejected by user, closing socket");
				close(ret);
			}
		}
	}

	void OnFiledescriptorWrite(int fd) final
	{}

	EventLoop::EventLoop& mEventLoop;
	IStreamSocketServerHandler* mHandler;

	int mFd = 0;

	//std::vector<StreamSocket> mConnections;
	std::vector<std::unique_ptr<StreamSocket>> mConnections; //TODO This is dumb, user can not access the actual connection

};

}

#endif // STREAMSOCKET_H
