#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include "EventLoop.h"

namespace Common {

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
		auto streamSocketLogger = spdlog::stdout_color_mt("UDPSocket");
		mLogger = spdlog::get("UDPSocket");

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

	}

	void StartListening(const char* localAddr, const uint16_t localPort) noexcept
	{

	}

	void Send(const char* data, size_t len, const char* dst = nullptr, const uint16_t port = 0) const noexcept
	{
		if(dst != nullptr)
		{
			struct sockaddr_in remote;
			remote.sin_addr.s_addr = ::inet_addr(dst);
			remote.sin_family = AF_INET;
			if(port == 0)
			{
				remote.sin_port = htons(mPort);
			}
			else
			{
				remote.sin_port = htons(port);
			}

			const auto ret = ::sendto(mFd, data, len, MSG_DONTWAIT, (struct sockaddr *)&remote, sizeof(struct sockaddr));

			if((ret == -1) && (errno == EINPROGRESS))
			{
				mLogger->error("UDP sendto failed with errno:{}", errno);
			}
		}
		else
		{
			mLogger->warn("Default addr not yet implemented, skipping send");
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

	uint16_t mPort = 0;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace Common

#endif // UDPSOCKET_H
