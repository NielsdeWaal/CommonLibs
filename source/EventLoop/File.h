#ifndef FILE_H
#define FILE_H

#include "DmaBuffer.h"
#include "EventLoop.h"
#include "UringCommands.h"
#include <cstdint>
#include <cstdio>
#include <fcntl.h>

constexpr std::uint32_t mMemoryDMAAlignment = 4096;
constexpr std::uint32_t mDiskReadDmaAlignment = 4096;
constexpr std::uint32_t mDiskWriteDmaAlignment = 4096;

// Problems to solve
// How can eventloop know which entity called for which read.
// This might be solved with the sqe_set_data and then have some key

class FileReader;

class IFileReaderHandler
{
public:
	virtual void OnFileOpen() = 0;  // io_uring_prep_openat with value 0
	virtual void OnFileClose() = 0; // io_uring_prep_close

	// TODO maybe write some buffer type that operates kinda like a string_view/span
	virtual void OnFileRead(void* buf, std::size_t len) = 0;        // io_uring_prep_read
	virtual void OnFileWrite(const void* buf, std::size_t len) = 0; // io_uring_prep_write
	virtual ~IFileReaderHandler() = default;
};

// Handler for file implementation
// This should enable us to create specialized file types which
// handle differently in the background and allow for differing api's
class IFileHandler
{
public:
	virtual ~IFileHandler() = default;
	virtual void OnFileOpen() = 0;
	virtual void OnWriteCompletion(std::size_t len) = 0;
	virtual void OnReadCompletion(std::size_t len, char* buf) = 0;
};

// Generic file backend for specilized file implementations
class UringFile : public EventLoop::IUringCallbackHandler
{
public:
	UringFile(EventLoop::EventLoop& ev, IFileHandler* fileHandler)
		: mEv(ev)
		, mHandler(fileHandler)
	{
		mLogger = mEv.RegisterLogger("UringFile");
	}

	void FileOpen(std::string filename, int flags)
	{
		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

		data->mCallback = this;
		data->mType = EventLoop::SourceType::Open;
		data->mInfo = EventLoop::OPEN{.filename = &filename, .flags = flags, .mode = S_IRUSR};

		mEv.QueueStandardRequest(std::move(data));
	}

	void ReadAt(std::uint64_t pos, std::size_t len)
	{
		mLogger->debug("Queueing read of size {}", len);
		auto buf = std::make_unique<char[]>(len);
		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();
		data->mCallback = this;
		data->mType = EventLoop::SourceType::Read;
		data->mInfo = EventLoop::READ{.fd = mFd, .buf = buf.release(), .len = len, .pos = pos};

		mEv.QueueStandardRequest(std::move(data));
	}

	void WriteAt(std::vector<char> buf, std::uint64_t pos)
	{
		mLogger->debug("Queueing write of size {}", buf.size());
		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();
		data->mCallback = this;
		data->mType = EventLoop::SourceType::Write;
		data->mInfo = EventLoop::WRITE{.fd = mFd, .buf = buf.data(), .len = buf.size(), .pos = pos};

		mEv.QueueStandardRequest(std::move(data));
	}
	void Append();

	void Close()
	{
		mLogger->debug("Closing file {}", mFd);
		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();
		data->mCallback = this;
		data->mType = EventLoop::SourceType::Close;
		data->mInfo = EventLoop::CLOSE{.fd = mFd};

		mEv.QueueStandardRequest(std::move(data));
	}

	void OnCompletion([[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override
	{
		switch(data->mType)
		{
		case EventLoop::SourceType::Open: {
			mLogger->debug("New UringFile opened");
			mFd = cqe.res;
			mIsOpen = true;
			mHandler->OnFileOpen();
			break;
		}
		case EventLoop::SourceType::Read: {
			auto buf = std::get<EventLoop::READ>(data->mInfo);
			mHandler->OnReadCompletion(cqe.res, static_cast<char*>(buf.buf));
			break;
		}
		case EventLoop::SourceType::Write: {
			mHandler->OnWriteCompletion(cqe.res);
			break;
		}
		case EventLoop::SourceType::Close: {
			break;
		}
		// There should be no other operations here
		default: {
			assert(false);
		}
		}
	}

private:
	EventLoop::EventLoop& mEv;
	IFileHandler* mHandler{};

	int mFd{0};
	bool mIsOpen{false};

	std::shared_ptr<spdlog::logger> mLogger;
};

class BufferedFile : public IFileHandler
{
public:
	~BufferedFile() override = default;

	BufferedFile(const BufferedFile&) = default;
	BufferedFile(BufferedFile&&) = delete;
	BufferedFile& operator=(const BufferedFile&) = default;
	BufferedFile& operator=(BufferedFile&&) = delete;

	BufferedFile(EventLoop::EventLoop& ev, const std::string& filename)
		: mEv(ev)
		, mFile(mEv, this)
	{
		mFile.FileOpen(filename, O_CREAT | O_RDWR);
	}

	void OnFileOpen() override
	{
		mIsOpen = true;
	}
	void OnReadCompletion(std::size_t len, char* buf) override
	{
		mReq.buf = buf;
		mReq.len = len;
		mReq.completed = true;
	}
	void OnWriteCompletion(std::size_t len) override
	{
		mReq.completed = true;
		mReq.len = len;
	}

	[[nodiscard]] bool IsOpen() const
	{
		return mIsOpen;
	}

	[[nodiscard]] bool ReqFinished() const
	{
		return mReq.completed;
	}

	[[nodiscard]] char* GetResBuf() const
	{
		return mReq.buf;
	}

	[[nodiscard]] std::size_t GetWriteSize() const
	{
		return mReq.len;
	}

	[[nodiscard]] std::size_t GetReadSize() const
	{
		return mReq.len;
	}

	void ReadAt(std::uint64_t pos, std::size_t len)
	{
		mFile.ReadAt(pos, len);
		mReq.completed = false;
		mReq.len = len;
		mReq.pos = pos;
	}

	void WriteAt(std::vector<char> buf, std::uint64_t pos)
	{
		mFile.WriteAt(buf, pos);
		mReq.completed = false;
		mReq.len = 0;
		mReq.pos = pos;
	}

	void FlushFile()
	{}

	void CloseFile()
	{
		mFile.Close();
		mIsOpen = false;
	}

private:
	struct OutstandingReq
	{
		std::size_t len;
		std::uint64_t pos;
		char* buf;
		bool completed{false};
	};
	EventLoop::EventLoop& mEv;
	UringFile mFile;
	bool mIsOpen{false};
	OutstandingReq mReq;
};

class DmaFile
{
public:
	DmaFile(EventLoop::EventLoop& ev, const std::string& filename)
		: mEv(ev)
	{
		OpenAt(filename);
	}

	~DmaFile()
	{
		if(mFd)
		{
			mEv.SubmitClose(mFd);
		}
	}

	EventLoop::uio::task<> OpenAt(const std::string filename)
	{
		// mFd = co_await mEv.SubmitOpenAt(filename.c_str(), O_CREAT | O_RDWR, S_IRUSR);
		// int ret = co_await mEv.SubmitOpenAt("/tmp/eventloop_coroutine_file", O_CREAT | O_RDWR, S_IRUSR);
		int ret = co_await mEv.SubmitOpenAt(filename.c_str(), O_CREAT | O_RDWR, S_IRUSR);
		mFd = ret;
	}

	EventLoop::SqeAwaitable WriteAt(EventLoop::DmaBuffer& buf, std::size_t pos)
	{
		return mEv.SubmitWrite(mFd, buf.GetPtr(), buf.GetSize(), pos);
	}

	EventLoop::SqeAwaitable Close()
	{
		auto res = mEv.SubmitClose(mFd);
		mFd = 0;
		return res;
	}

	[[nodiscard]] bool IsOpen() const
	{
		return mFd != 0;
	}

private:
	EventLoop::EventLoop& mEv;
	int mFd{0};
};

// class FileReader
// {
// public:
// 	explicit FileReader(EventLoop::EventLoop& ev)
// 		: mEv(ev)
// 	{}

// 	void OpenFile(); // Open the file pointed to by path

// 	void WithFile(const std::string& path);

// private:
// 	EventLoop::EventLoop& mEv;
// 	// CircularBuffer<std::array<char, 4096>> mReadBuffer;
// 	// Want to be able to queue multiple reads
// 	// Or we use a single linear buffer to put reads into
// };

// class CommonLibsFile
// {
// public:
// 	CommonLibsFile() = default;

// 	/**
// 	 * @brief Open file from existing filedescriptor
// 	 */
// 	explicit CommonLibsFile(int fd)
// 		: mFd(fd)
// 	{}

// private:
// 	int mFd;
// };

// class file_impl;

// class file
// {
// public:
// 	file() = default;

// private:
// 	file_impl mImpl;
// };

// class file_impl
// {
// public:
// 	std::size_t write_dma(std::uint64_t pos, const void* buffer, std::size_t len);
// 	// future<size_t> write_dma(uint64_t pos, std::vector<iovec> iov, const io_priority_class& pc) = 0;
// 	std::size_t read_dma(std::uint64_t pos, void* buffer, std::size_t len);
// 	// future<size_t> read_dma(uint64_t pos, std::vector<iovec> iov, const io_priority_class& pc) = 0;
// 	// future<> flush(void) = 0;
// 	// future<struct stat> stat(void) = 0;
// 	// future<> truncate(uint64_t length) = 0;
// 	// future<> discard(uint64_t offset, uint64_t length) = 0;
// 	// future<> allocate(uint64_t position, uint64_t length) = 0;
// 	// future<uint64_t> size(void) = 0;
// 	// future<> close() = 0;
// 	// std::unique_ptr<file_handle_impl> dup();
// 	// subscription<directory_entry> list_directory(std::function<future<>(directory_entry de)> next) = 0;
// 	// future<temporary_buffer<uint8_t>> dma_read_bulk(
// 	// 	uint64_t offset, size_t range_size, const io_priority_class& pc) = 0;

// private:
// };

#endif // FILE_H
