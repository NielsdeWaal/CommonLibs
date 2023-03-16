#include <catch2/catch.hpp>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>

#include "EventLoop.h"
#include "File.h"
#include "UringCommands.h"

TEST_CASE("Coroutine file creation", "[EventLoop Coroutine]")
{
	struct Handler : public EventLoop::IEventLoopCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		// , mFile(mEv, "/tmp/eventloop_file")
		{
			mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
		}

		void OnEventLoopCallback() final
		{
			func();
		}

		EventLoop::uio::task<> func() {
			if(!mSubmitted)
			{
				mSubmitted = true;
				int ret = co_await mEv.SubmitOpenAt("/tmp/eventloop_coroutine_file", O_CREAT, S_IRUSR);
				REQUIRE(ret != 0);
				mEv.Stop();
			}
		}

	private:
		EventLoop::EventLoop& mEv;
		bool mSubmitted{false};
		// BufferedFile mFile;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_coroutine_file");
}

TEST_CASE("Coroutine file read/write", "[EventLoop Coroutine]")
{
	struct Handler : public EventLoop::IEventLoopCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		// , mFile(mEv, "/tmp/eventloop_file")
		{
			for(int i = 0; i < 100; ++i)
			{
				mTestData.at(i) = i;
			}

			mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
		}

		void OnEventLoopCallback() final
		{
			func();
		}

		EventLoop::uio::task<> func() {
			if(!mSubmitted)
			{
				mSubmitted = true;
				int ret = co_await mEv.SubmitOpenAt("/tmp/eventloop_coroutine_file", O_CREAT | O_RDWR, S_IRUSR);
				REQUIRE(ret != 0);
				INFO("Got fd " << ret);
				mFd = ret;

				int size = co_await mEv.SubmitWrite(mFd, mTestData.data(), 100, 0);
				REQUIRE(size == 100);

				std::array<std::uint8_t, 100> verification{};
				size = co_await mEv.SubmitRead(mFd, 0, verification.data(), verification.size());

				REQUIRE(size == 100);
				for(int i = 0; i < 100; ++i)
				{
					REQUIRE(mTestData.at(i) == verification.at(i));
				}
				
				mEv.Stop();
			}
		}

	private:
		EventLoop::EventLoop& mEv;
		bool mSubmitted{false};
		std::array<std::uint8_t, 100> mTestData{};
		int mFd;
		// BufferedFile mFile;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_coroutine_file");
}
