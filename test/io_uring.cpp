#include <catch2/catch.hpp>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

#include <memory>

#include "EventLoop.h"
#include "File.h"
#include "UringCommands.h"

// TEST_CASE("File open", "[EventLoop file]")
// {
// 	EventLoop::EventLoop loop;
// 	loop.LoadConfig("Example.toml");
// 	loop.Configure();

// 	// const std::string filename = std::string{"/tmp/"} + std::string{"test.file"};
// 	// int fd = ::open(filename.data(), O_RDWR | O_CLOEXEC);

// 	// loop.RegisterFile(fd);

// 	// Create file handler with callback in order to request read after write has been done.

// 	loop.Run();
// }

TEST_CASE("io_uring nop", "[EventLoop io_uring]")
{
	struct Handler : public EventLoop::IUringCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			auto data = std::make_unique<EventLoop::UserData>();

			data->mCallback = this;
			data->mType = EventLoop::SourceType::Nop;

			mEv.QueueStandardRequest(std::move(data));
		}

		void OnCompletion(
			[[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
		{
			REQUIRE(data->mType == EventLoop::SourceType::Nop);
			mEv.Stop();
		}

	private:
		EventLoop::EventLoop& mEv;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();
}

TEST_CASE("io_uring open", "[EventLoop io_uring]")
{
	struct Handler : public EventLoop::IUringCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

			data->mCallback = this;
			data->mType = EventLoop::SourceType::Open;
			data->mInfo =
				EventLoop::OPEN{.filename = new std::string{"/tmp/io_uring_test"}, .flags = O_CREAT, .mode = S_IRUSR};

			mEv.QueueStandardRequest(std::move(data));
		}

		void OnCompletion(
			[[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
		{
			// auto data = reinterpret_cast<EventLoop::UserData>(cqe.user_data);
			REQUIRE(data->mType == EventLoop::SourceType::Open);
			REQUIRE(access("/tmp/io_uring_test", F_OK) == 0);
			// FIXME this should not have to be done by the user, either store string in the struct or move to smart
			// pointer
			const auto& openData = std::get<EventLoop::OPEN>(data->mInfo);
			delete openData.filename;
			// REQUIRE(true);
			mEv.Stop();
		}

	private:
		EventLoop::EventLoop& mEv;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/io_uring_test");
}

TEST_CASE("io_uring write", "[EventLoop io_uring]")
{
	struct Handler : public EventLoop::IUringCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			for(int i = 0; i < 100; ++i)
			{
				mTestData.at(i) = i;
			}

			std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

			data->mCallback = this;
			data->mType = EventLoop::SourceType::Open;
			data->mInfo = EventLoop::OPEN{
				.filename = new std::string{"/tmp/io_uring_test"}, .flags = O_CREAT | O_RDWR, .mode = S_IRUSR};

			mEv.QueueStandardRequest(std::move(data));
		}

		void OnCompletion(
			[[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
		{
			switch(data->mType)
			{
			case EventLoop::SourceType::Open: {
				REQUIRE(true);
				std::unique_ptr<EventLoop::UserData> writeData = std::make_unique<EventLoop::UserData>();

				writeData->mCallback = this;
				writeData->mType = EventLoop::SourceType::Write;
				writeData->mInfo =
					EventLoop::WRITE{.fd = cqe.res, .buf = mTestData.data(), .len = mTestData.size(), .pos = 0};

				mEv.QueueStandardRequest(std::move(writeData));

				mFd = cqe.res;
				break;
			}
			case EventLoop::SourceType::Write: {
				std::array<std::uint8_t, 100> verification{};
				int ret = ::read(mFd, verification.data(), verification.size());

				REQUIRE(ret == 100);

				for(int i = 0; i < 100; ++i)
				{
					REQUIRE(mTestData.at(i) == verification.at(i));
				}

				mEv.Stop();
				break;
			}
			default: {
				// This case should never be hit
				REQUIRE(false);
				mEv.Stop();
				break;
			}
			}
		}

	private:
		EventLoop::EventLoop& mEv;
		int mFd{0};
		std::array<std::uint8_t, 100> mTestData{};
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();
	unlink("/tmp/io_uring_test");
}

TEST_CASE("io_uring read", "[EventLoop io_uring]")
{
	// NOTE technically there is a weird dependency on the write test here
	// For purity, this should be changed such that it does a standard sync write followed by a
	// uring read
	struct Handler : public EventLoop::IUringCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			for(int i = 0; i < 100; ++i)
			{
				mTestData.at(i) = i;
			}

			std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

			data->mCallback = this;
			data->mType = EventLoop::SourceType::Open;
			data->mInfo = EventLoop::OPEN{
				.filename = new std::string{"/tmp/io_uring_test"}, .flags = O_CREAT | O_RDWR, .mode = S_IRUSR};

			mEv.QueueStandardRequest(std::move(data));
		}

		void OnCompletion(
			[[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
		{
			switch(data->mType)
			{
			case EventLoop::SourceType::Open: {
				REQUIRE(true);
				std::unique_ptr<EventLoop::UserData> writeData = std::make_unique<EventLoop::UserData>();

				writeData->mCallback = this;
				writeData->mType = EventLoop::SourceType::Write;
				writeData->mInfo =
					EventLoop::WRITE{.fd = cqe.res, .buf = mTestData.data(), .len = mTestData.size(), .pos = 0};

				mEv.QueueStandardRequest(std::move(writeData));

				mFd = cqe.res;
				break;
			}
			case EventLoop::SourceType::Write: {
				REQUIRE(true);
				std::unique_ptr<EventLoop::UserData> readData = std::make_unique<EventLoop::UserData>();

				readData->mCallback = this;
				readData->mType = EventLoop::SourceType::Read;
				readData->mInfo =
					EventLoop::READ{.fd = mFd, .buf = mVerification.data(), .len = mVerification.size(), .pos = 0};

				mEv.QueueStandardRequest(std::move(readData));
				break;
			}
			case EventLoop::SourceType::Read: {
				REQUIRE(cqe.res == 100);

				for(int i = 0; i < 100; ++i)
				{
					REQUIRE(mTestData.at(i) == mVerification.at(i));
				}

				mEv.Stop();
				break;
			}
			default: {
				// This case should never be hit
				REQUIRE(false);
				mEv.Stop();
				break;
			}
			}
		}

	private:
		EventLoop::EventLoop& mEv;
		int mFd{0};
		std::array<std::uint8_t, 100> mTestData{};
		std::array<std::uint8_t, 100> mVerification{};
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();
	unlink("/tmp/io_uring_test");
}

TEST_CASE("io_uring close", "[EventLoop io_uring]")
{
	struct Handler : public EventLoop::IUringCallbackHandler
	{
		explicit Handler(EventLoop::EventLoop& ev)
			: mEv(ev)
		{
			std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

			data->mCallback = this;
			data->mType = EventLoop::SourceType::Open;
			data->mInfo =
				EventLoop::OPEN{.filename = new std::string{"/tmp/io_uring_test"}, .flags = O_CREAT, .mode = S_IRUSR};

			mEv.QueueStandardRequest(std::move(data));
		}

		void OnCompletion(
			[[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
		{
			switch(data->mType)
			{
			case EventLoop::SourceType::Open: {
				REQUIRE(true);
				std::unique_ptr<EventLoop::UserData> closeData = std::make_unique<EventLoop::UserData>();

				closeData->mCallback = this;
				closeData->mType = EventLoop::SourceType::Close;
				closeData->mInfo = EventLoop::CLOSE{.fd = cqe.res};

				mEv.QueueStandardRequest(std::move(closeData));
				break;
			}
			case EventLoop::SourceType::Close: {
				REQUIRE(true);

				mEv.Stop();
				break;
			}
			default: {
				// This case should never be hit
				REQUIRE(false);
				mEv.Stop();
				break;
			}
			}
		}

	private:
		EventLoop::EventLoop& mEv;
	};

	EventLoop::EventLoop loop;
	loop.LoadConfig("Example.toml");
	loop.Configure();

	Handler test(loop);

	loop.Run();

	unlink("/tmp/io_uring_test");
}

// TEST_CASE("File open", "[EventLoop file]")
// {
// 	TmpDir t;
// 	FileFlags flags = FileFlags::RW | FileFlags::Create | FileFlags::Exclusive;
// 	FileAccessFlags aFlags = FileAccessFlags::Read | FileAccessFlags::Write;
// 	std::string filename = (t.GetPath() / "test.tmp").native();

// 	struct Handler : public IFileReaderHandler
// 	{
// 	public:
// 		explicit Handler(EventLoop::EventLoop& ev)
// 			: mEv(ev)
// 			, mReader(ev)
// 		{
// 			mReader.SetReadAhead(1);
// 			mReader.SetBufferSize(512);
// 			mReader.OpenFile(filename);
// 		}

// 		void OnFileOpen(File file) override
// 		{
// 			REQUIRE(true);
// 			file.close();
// 		}

// 		void OnFileClose() override
// 		{
// 			int exists = ::access(filename, aFlags);
// 			REQUIRE(exists == 0);
// 			std::raise(SIGINT);
// 		}
// 		void OnFileRead(void* /*buf*/, std::size_t /*len*/) override
// 		{}
// 		void OnFileWrite(const void* /*buf*/, std::size_t /*len*/) override
// 		{}

// 	private:
// 		EventLoop::EventLoop& mEv;
// 		FileReader mReader;
// 	};
// }

// TEST_CASE("File open", "[EventLoop file]")
// {
// 	struct Handler : public IFileReaderHandler
// 	{
// 	public:
// 		explicit Handler(EventLoop::EventLoop& ev)
// 			: mEv(ev)
// 			, mReader(ev)
// 		{
// 			mReader.OpenFile();
// 		}

// 		void OnFileOpen() override
// 		{
// 			REQUIRE(true);
// 		}

// 		void OnFileClose() override
// 		{
// 			REQUIRE(true);
// 			std::raise(SIGINT);
// 		}
// 		void OnFileRead(void* /*buf*/, std::size_t /*len*/) override
// 		{}
// 		void OnFileWrite(const void* /*buf*/, std::size_t /*len*/) override
// 		{}

// 	private:
// 		EventLoop::EventLoop& mEv;
// 		FileReader mReader;
// 	};

// 	EventLoop::EventLoop loop;
// 	loop.LoadConfig("Example.toml");
// 	loop.Configure();

// 	Handler handler(loop);

// 	loop.Run();
// }

// TEST_CASE("File read/write", "[EventLoop file]")
// {
// 	struct Handler : public IFileReaderHandler
// 	{
// 	public:
// 		explicit Handler(EventLoop::EventLoop& ev)
// 			: mEv(ev)
// 			, mReader(ev)
// 		{
// 			mReader.OpenFile();
// 		}

// 		void OnFileOpen() override
// 		{}

// 		void OnFileClose() override
// 		{}

// 		void OnFileRead(void* /*buf*/, std::size_t /*len*/) override
// 		{}
// 		void OnFileWrite(const void* /*buf*/, std::size_t /*len*/) override
// 		{}

// 	private:
// 		EventLoop::EventLoop& mEv;
// 		FileReader mReader;
// 	};

// 	EventLoop::EventLoop loop;
// 	loop.LoadConfig("Example.toml");
// 	loop.Configure();

// 	Handler handler(loop);

// 	loop.Run();
// }
