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

	// unlink("/tmp/eventloop_coroutine_file");
}
