#include <catch2/catch.hpp>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>

#include "DmaBuffer.h"
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
				mFile.CloseFile();
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
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_file");
}

TEST_CASE("Dma file creation", "[EventLoop File]")
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
		DmaFile mFile;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_file");
}

TEST_CASE("Dma file read/write", "[EventLoop File]")
{
	struct Handler //: public EventLoop::IEventLoopCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
			// , mFile(mEv, "/tmp/eventloop_file")
			, mFile(mEv)
		{
			// mEv.RegisterCallbackHandler(this, EventLoop::EventLoop::LatencyType::Low);
			func();
		}

		// void OnEventLoopCallback() final
		// {
		// 	if(mFile.IsOpen())
		// 	{
		// 		REQUIRE(access("/tmp/eventloop_file", F_OK) == 0);
		// 		func();
		// 	}
		// }

		EventLoop::uio::task<> func()
		{
			// mFile.Create("/tmp/eventloop_file");
			// int a = co_await mFile.OpenAt("/tmp/eventloop_file");
			co_await mFile.OpenAt("/tmp/eventloop_file");

			// EventLoop::DmaBuffer testBuf = mEv.AllocateDmaBuffer(4096);
			EventLoop::DmaBuffer testBuf{4096};
			int ret = co_await mFile.WriteAt(testBuf, 0);
			REQUIRE(ret == 4096);

			// EventLoop::DmaBuffer verificationBuf = mEv.AllocateDmaBuffer(4096);
			// ret = co_await mFile.ReadAt(verificationBuf, 0);
			// REQUIRE(ret == 4096);
			EventLoop::DmaBuffer verBuf = co_await mFile.ReadAt(0, 4096);
			REQUIRE(verBuf.GetSize() == 4096);

			// co_await mFile.Close();

			verBuf.Free();

			mEv.Stop();
		}

	private:
		EventLoop::EventLoop& mEv;
		DmaFile mFile;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_file");
}

TEST_CASE("Dma append-only file", "[EventLoop File]")
{
	struct Handler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
			, mFile(mEv)
		{
			func();
		}

		EventLoop::uio::task<> func()
		{
			// mFile.Create("/tmp/eventloop_file");
			// int a = co_await mFile.OpenAt("/tmp/eventloop_file");
			co_await mFile.OpenAt("/tmp/eventloop_file");

			// EventLoop::DmaBuffer testBuf = mEv.AllocateDmaBuffer(4096);
			EventLoop::DmaBuffer testBuf{4096};
			int ret = co_await mFile.Append(testBuf);
			REQUIRE(ret == 4096);

			// EventLoop::DmaBuffer verificationBuf = mEv.AllocateDmaBuffer(4096);
			// ret = co_await mFile.ReadAt(verificationBuf, 0);
			// REQUIRE(ret == 4096);
			EventLoop::DmaBuffer verBuf = co_await mFile.ReadAt(0, 4096);
			REQUIRE(verBuf.GetSize() == 4096);

			// co_await mFile.Close();

			verBuf.Free();

			mEv.Stop();
		}

	private:
		EventLoop::EventLoop& mEv;
		AppendOnlyFile mFile;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/eventloop_file");
}
