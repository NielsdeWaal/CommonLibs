#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "EventLoop.h"

namespace Common {

using namespace EventLoop;

/**
 * Afer socket has been created, we can send data.
 * Do we need to specify dest addr beforehand as a shortcut?
 * Or can we just have it default to a set addr when no addr is present in send call?
 */

class UDPSocket;

//TODO Implement interface features
class IUDPSocketHandler
{
public:
	//virtual void OnConnected() = 0;
	//virtual void OnDisconnect() = 0;
	//virtual void OnIncomingData(char* data, size_t len) = 0;
	virtual ~IUDPSocketHandler() {}
};

class UDPSocket : public EventLoop::IFiledescriptorCallbackHandler
{
public:
	UDPSocket(EventLoop::EventLoop& ev, IUDPSocketHandler* handler) noexcept
		: mEventLoop(ev)
		, mHandler(handler)
		, mFd(::socket(AF_INET, SOCK_DGRAM, 0))
	{
		mLogger = spdlog::get("UDPSocket");
		if(mLogger == nullptr)
		{
			auto UDPSocketLogger = spdlog::stdout_color_mt("UDPSocket");
			mLogger = spdlog::get("UDPSocket");
		}

		fcntl(mFd, F_SETFL, O_NONBLOCK);
	}

	void SetDefaultAddress(const char* addr) noexcept
	{
		mAddrSet = true;
	}

	void SetPort(const uint16_t port) noexcept
	{
		mPort = port;
	}

	void Connect(const char* addr, const uint16_t port) noexcept
	{
		assert(port > 0);

		remote.sin_addr.s_addr = ::inet_addr(addr);
		//remote.sin_addr.s_addr = addr;
		remote.sin_family = AF_INET;
		remote.sin_port = htons(port);

		const int ret = ::connect(mFd, (struct sockaddr *)&remote, sizeof(struct sockaddr));

		//TODO Handle error case
		if((ret == -1) && (errno == EINPROGRESS))
		{
			mLogger->critical("Connect failed, code:{}", ret);
			//throw std::runtime_error("Connect failed");
			//mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN | EPOLLOUT, this);
		}

		mPort = port;
	}

	void StartListening(const char* localAddr, const uint16_t localPort) noexcept
	{

	}

	void Send(const char* data, size_t len, const char* dst = nullptr, const uint16_t port = 0) const noexcept
	{
		if(dst != nullptr)
		{
			struct sockaddr_in tempRemote;
			tempRemote.sin_addr.s_addr = ::inet_addr(dst);
			tempRemote.sin_family = AF_INET;
			if(port == 0)
			{
				tempRemote.sin_port = htons(mPort);
			}
			else
			{
				tempRemote.sin_port = htons(port);
			}

			const auto ret = ::sendto(mFd, data, len, MSG_DONTWAIT, (struct sockaddr *)&tempRemote, sizeof(struct sockaddr));

			if((ret == -1) && (errno == EINPROGRESS))
			{
				mLogger->error("UDP sendto failed with errno:{}", errno);
			}
		}
		else
		{
			const auto ret = ::send(mFd, data, len, MSG_DONTWAIT);

			if((ret == -1) && (errno == EINPROGRESS))
			{
				mLogger->error("UDP send failed with errno:{}", errno);
			}
		}
	}

private:
	void OnFiledescriptorWrite(int fd) final
	{

	}

	void OnFiledescriptorRead(int fd) final
	{

	}

private:
	EventLoop::EventLoop& mEventLoop;
	IUDPSocketHandler* mHandler;

	const int mFd = 0;
	bool mAddrSet = false;

	struct sockaddr_in remote;

	uint16_t mPort = 0;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace Common

#endif // UDPSOCKET_H
