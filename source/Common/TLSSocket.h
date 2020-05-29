#ifndef TLS_SOCKET_H
#define TLS_SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace Common {

class TLSSocket;

class ITLSSocketHandler
{
public:
	virtual void OnConnected() = 0;
	virtual void OnDisconnect(TLSSocket* conn) = 0;
	virtual void OnIncomingData(TLSSocket* conn, char* data, std::size_t len) = 0;
	virtual ~ITLSSocketHandler() {}
};

/**
 * @brief Async socket with TLS handling at both endpoints
 *
 * This socket will load openssl to use for TLS handling.
 * Socket interface is equal to that of the StreamSocket.
 */
class TLSSocket : public EventLoop::IFiledescriptorCallbackHandler
{
public:
	TLSSocket(EventLoop::EventLoop& ev, ITLSSocketHandler* handler)
		: mEventLoop(ev)
		, mHandler(handler)
		, readBuf(std::make_unique<std::array<char, 65535>>())
	{
		mLogger = mEventLoop.RegisterLogger("TLSSocket");

		mFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

		SSL_library_init();

		OpenSSL_add_all_algorithms();  /* Load cryptos, et.al. */
		SSL_load_error_strings();   /* Bring in and register error messages */
		mSSLMethod = TLS_client_method();  /* Create new client-method instance */
		mCTX = SSL_CTX_new(mSSLMethod);   /* Create new context */
		if(!mCTX)
		{
			ERR_print_errors_fp(stderr);
			throw std::runtime_error("");
		}

		SSL_CTX_set_default_read_buffer_len(mCTX, 65535);

		mSSL = SSL_new(mCTX);

		SSL_set_fd(mSSL, mFd);

	}

	~TLSSocket()
	{
		if(mConnected && mSSLConnected)
		{
			Shutdown();
		}
	}

	void Connect(const char* addr, const uint16_t port) noexcept
	{
		remote.sin_addr.s_addr = ::inet_addr(addr);
		//remote.sin_addr.s_addr = addr;
		remote.sin_family = AF_INET;
		remote.sin_port = htons(port);

		const int ret = ::connect(mFd, (struct sockaddr *)&remote, sizeof(struct sockaddr));

		//TODO Handle error case
		if((ret == -1) && (errno == EINPROGRESS))
		{
			//mLogger->critical("Connect failed, code:{}", ret);
			//throw std::runtime_error("Connect failed");
			mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN | EPOLLOUT, this);
		}
		else
		{
			mEventLoop.RegisterFiledescriptor(mFd, EPOLLIN, this);
			mLogger->info("fd:{} connected instantly", mFd);
		}
	}

	void ConnectHostname(const std::string& url, const uint16_t port) noexcept
	{
		struct addrinfo hints;
		struct addrinfo *infoptr;
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET; // AF_INET means IPv4 only addresses
		hints.ai_socktype = SOCK_STREAM; // Only want stream-based connection
		hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

		const int result = ::getaddrinfo(url.c_str(), NULL, &hints, &infoptr);
		if(result)
		{
			//fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
			mLogger->critical("getaddrinfo: {}", gai_strerror(result));
			throw std::runtime_error("getaddrinfo");
		}

		//struct addrinfo *p;
		char host[256];
		getnameinfo(infoptr->ai_addr, infoptr->ai_addrlen, host, sizeof (host), NULL, 0, NI_NUMERICHOST);
		mLogger->info("Resolved {} to {}", url, std::string{host});

		Connect(host, port);

		freeaddrinfo(infoptr);
	}

	void Send(const char* data, const size_t len) noexcept
	{
		if(mConnected && mSSLConnected)
		{
			//::send(mFd, data, len, MSG_DONTWAIT);
			const int ret = SSL_write(mSSL, data, len);
			if(ret <= 0)
			{
				mLogger->error("Unable to send data, closing socket");
				mConnected = false;
				mSSLConnected = false;
				mHandler->OnDisconnect(this);
				::close(mFd);
			}
		}
		else
		{
			mLogger->warn("Attempted send on fd:{}, while not connected", mFd);
		}
	}

	void Shutdown() noexcept
	{
		if(mConnected && mSSLConnected)
		{
			const int ret = SSL_shutdown(mSSL);
			if(ret == 0)
			{
				mLogger->debug("SSL shutdown initiated, will complete at a later point in time");
				return;
			}
			else if(ret == 1)
			{
				mEventLoop.UnregisterFiledescriptor(mFd);
				mSSLConnected = false;
				mConnected = false;
				::close(mFd);
				return;
			}
			else
			{
				mSSLShutdownRunning = true;
			}
		}
	}

private:
	void OnFiledescriptorWrite(int fd) final
	{
		if(!mConnected)
		{
			int err = 0;
			socklen_t len = sizeof(int);
			const int status = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
			if (status != -1)
			{
				if (err == 0)
				{
					//mEventLoop.ModifyFiledescriptor(fd, EPOLLIN | EPOLLRDHUP, this);
					mConnected = true;
					mLogger->info("Connection establisched on fd:{}, starting SSL handshake", fd);
					const int ret = SSL_connect(mSSL);
					if(ret < 0)
					{
						err = SSL_get_error(mSSL, ret);
						if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE ||
							err == SSL_ERROR_WANT_X509_LOOKUP)
						{
							return;
						}
					}
					//mHandler->OnConnected();
				}
				else
				{
					mEventLoop.UnregisterFiledescriptor(mFd);
					mHandler->OnDisconnect(this);
					mConnected = false;
				}
			}
		}
		else if(!mSSLConnected)
		{
			const int ret = SSL_connect(mSSL);
			if(ret < 0)
			{
				const auto err = SSL_get_error(mSSL, ret);
				if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE ||
					 err == SSL_ERROR_WANT_X509_LOOKUP)
				{
					return;
				}
			}
			else
			{
				mSSLConnected = true;
				mLogger->info("TLS connected");
				mHandler->OnConnected();
			}
		}
	}

	void OnFiledescriptorRead(int fd) final
	{
		if(!mSSLConnected)
		{
			// We have to call SSL_connect again to see if the handshake has finished.
			// When there is no error returned we know that our handshake has finished.
			const int ret = SSL_connect(mSSL);
			if(ret < 0)
			{
				const int err = SSL_get_error(mSSL, ret);
				if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE ||
					 err == SSL_ERROR_WANT_X509_LOOKUP)
				{
					return;
				}
			}
			else
			{
				mSSLConnected = true;
			}
		}
		else
		{
			std::size_t len = SSL_read(mSSL, readBuf->data(), sizeof(readBuf));
			// const std::size_t remaining = SSL_pending(mSSL);
			// mLogger->warn("{} bytes remaining to be read", remaining);
			while(SSL_pending(mSSL) > 0)
			{
				len += SSL_read(mSSL, readBuf->data() + len, sizeof(readBuf));
			}

			const int err = SSL_get_error(mSSL, len);
			if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE ||
				 err == SSL_ERROR_WANT_X509_LOOKUP)
			{
				return;
			}

			if(len == 0)
			{
				mLogger->info("Socket has been disconnected, closing filedescriptor. fd:{}", fd);
				mEventLoop.UnregisterFiledescriptor(mFd);
				mHandler->OnDisconnect(this);
				mConnected = false;
				mSSLConnected = false;
				return;
			}

			mHandler->OnIncomingData(this, readBuf->data(), len);
		}
	}

	EventLoop::EventLoop& mEventLoop;
	ITLSSocketHandler* mHandler;

	int mFd = 0;
	struct sockaddr_in remote;
	bool mConnected = false;
	bool mSSLConnected = false;
	bool mSSLShutdownRunning = false;

	// TODO Maybe allocate this on the heap in order to see
	// if we can read more then 4k bytes.
	// std::array<char, 65535> readBuf = {0};
	std::unique_ptr<std::array<char, 65535>> readBuf;

	//SSL/TLS relevant members
	const SSL_METHOD *mSSLMethod;
	SSL_CTX* mCTX;
	SSL* mSSL;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // TLS_SOCKET_H
