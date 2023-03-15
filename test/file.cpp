#include <catch2/catch.hpp>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>

#include "EventLoop.h"
#include "File.h"

TEST_CASE("Buffered file creation", "[EventLoop File]")
{
	struct Handler : public EventLoop::IEventLoopCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
			, mFile(mEv, "/tmp/eventloop_file")
		{
			mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
		}

		void OnEventLoopCallback() final
		{
			if(mFile.IsOpen())
			{
				REQUIRE(access("/tmp/eventloop_file", F_OK) == 0);
				mEv.Stop();
			}
		}

	private:
		EventLoop::EventLoop& mEv;
		BufferedFile mFile;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_file");
}

TEST_CASE("Buffered file operations", "[EventLoop File]")
{
	struct Handler : public EventLoop::IEventLoopCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
			, mFile(mEv, "/tmp/eventloop_file")
		{
			mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
		}

		void OnEventLoopCallback() final
		{
			if(mFile.ReqFinished() && !mWriteFinished && mOpened)
			{
				REQUIRE(mFile.GetWriteSize() == 11);
				mWriteFinished = true;
				mFile.ReadAt(0, 11);
			}
			else if(mFile.ReqFinished() && mWriteFinished && mOpened)
			{
				char* buf = mFile.GetResBuf();
				REQUIRE(mFile.GetReadSize() == 11);
				mEv.Stop();
			}
			else if(mFile.IsOpen() && !mOpened)
			{
				REQUIRE(access("/tmp/eventloop_file", F_OK) == 0);
				mOpened = true;
				mFile.WriteAt(std::vector<char>{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'}, 0);
			}
		}

	private:
		EventLoop::EventLoop& mEv;
		BufferedFile mFile;

		bool mOpened{false};
		bool mWriteFinished{false};
		bool mReadFinished{false};
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_file");
}