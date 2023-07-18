#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "EventLoop.h"

namespace Common {

/**
 * Afer socket has been created, we can send data.
 * Do we need to specify dest addr beforehand as a shortcut?
 * Or can we just have it default to a set addr when no addr is present in send call?
 */

class UDPSocket;

// TODO Implement interface features
class IUDPSocketHandler
{
public:
	// virtual void OnConnected() = 0;
	// virtual void OnDisconnect() = 0;
	virtual void OnIncomingData(char* data, size_t len) = 0;
	virtual ~IUDPSocketHandler()
	{}
};

class UDPSocket : public EventLoop::IFiledescriptorCallbackHandler
{
public:
	UDPSocket(EventLoop::EventLoop& ev, IUDPSocketHandler* handler) noexcept
		: mEventLoop(ev)
		, mHandler(handler)
		, mFd(::socket(AF_INET, SOCK_DGRAM, 0))
		, readBuf(static_cast<char*>(std::aligned_alloc(512, 32000)))
	{
		mLogger = mEventLoop.RegisterLogger("UDPSocket");

		fcntl(mFd, F_SETFL, O_NONBLOCK);
	}

	void SetDefaultAddress([[maybe_unused]] const char* addr) noexcept
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
		// remote.sin_addr.s_addr = addr;
		remote.sin_family = AF_INET;
		remote.sin_port = htons(port);

		const int ret = ::connect(mFd, (struct sockaddr*)&remote, sizeof(struct sockaddr));

		// TODO Handle error case
		if((ret == -1) && (errno == EINPROGRESS))
		{
			mLogger->critical("Connect failed, code:{}", ret);
			// throw std::runtime_error("Connect failed");
			// mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN | EPOLLOUT, this);
		}

		mPort = port;
	}

	void StartListening([[maybe_unused]] const char* localAddr, [[maybe_unused]] const uint16_t localPort) noexcept
	{
		assert(localPort > 0);

		remote.sin_family = AF_INET;
		remote.sin_port = htons(localPort);
		remote.sin_addr.s_addr = htonl(INADDR_ANY);

		mLogger->info("Binding UDP socket to {}", localPort);

		const int ret = ::bind(mFd, (struct sockaddr*)&remote, sizeof(struct sockaddr));

		if(ret == -1) {
			mLogger->critical("Failed to bind socket: {}", ret);
		}

		
		mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN | EPOLLOUT, this);
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

			const auto ret =
				::sendto(mFd, data, len, MSG_DONTWAIT, (struct sockaddr*)&tempRemote, sizeof(struct sockaddr));

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
	void OnFiledescriptorWrite([[maybe_unused]] int fd) final
	{
		// mLogger->info("File descriptor write");
	}

	void OnFiledescriptorRead([[maybe_unused]] int fd) final
	{
		mLogger->trace("File descriptor read");
		// recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen))
		struct sockaddr_in in;
		std::uint32_t sockLen = sizeof(in);
		const std::size_t len = ::recvfrom(mFd, readBuf.get(), 32000, 0, (struct sockaddr*)&in, &sockLen);

		if(len > 0) {
			mLogger->debug("Received from: {}", inet_ntoa(in.sin_addr));
			mHandler->OnIncomingData(readBuf.get(), len);
		}
	}

private:
	EventLoop::EventLoop& mEventLoop;
	IUDPSocketHandler* mHandler;

	const int mFd = 0;
	bool mAddrSet = false;

	struct sockaddr_in remote;

	uint16_t mPort = 0;

	// std::array<char, 5000> readBuf{0};
	std::unique_ptr<char> readBuf;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace Common

#endif // UDPSOCKET_H
