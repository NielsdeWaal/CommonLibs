#ifndef FILE_H
#define FILE_H

#include "EventLoop.h"
#include "UringCommands.h"
#include <cstdio>

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
	virtual void OnWriteCompletion() = 0;
	virtual void OnReadCompletion() = 0;
};

// Generic file backend for specilized file implementations
class UringFile : public EventLoop::IUringCallbackHandler
{
public:
	UringFile(EventLoop::EventLoop& ev, IFileHandler* fileHandler)
		: mEv(ev)
		, mHandler(fileHandler)
	{}

	void FileOpen(std::string filename, int flags)
	{
		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

		data->mCallback = this;
		data->mType = EventLoop::SourceType::Open;
		data->mInfo =
			EventLoop::OPEN{.filename = &filename, .flags = flags, .mode = S_IRUSR};

		mEv.QueueStandardRequest(std::move(data));
	}

	void WriteAt();
	void Append();

	void ReadAt();

	void OnCompletion(
			[[maybe_unused]] EventLoop::CompletionQueueEvent& cqe, const EventLoop::UserData* data) override 
	{
		switch(data->mType) {
			case EventLoop::SourceType::Open: {
				mFd = cqe.res;
				mIsOpen = true;
				break;
			}
			case EventLoop::SourceType::Read: {
				mHandler->OnReadCompletion();
				break;
			}
			case EventLoop::SourceType::Write: {
				mHandler->OnWriteCompletion();
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
	{
		std::unique_ptr<EventLoop::UserData> data = std::make_unique<EventLoop::UserData>();

		data->mCallback = this;
		data->mType = EventLoop::SourceType::Open;
		data->mInfo = EventLoop::OPEN{.filename = filename, .flags = O_CREAT, .mode = S_IRUSR};

		mEv.QueueStandardRequest(std::move(data));
	}

	void OnFileOpen() override;
	void OnWriteCompletion() override;
	void OnReadCompletion() override;

	[[nodiscard]] bool IsOpen() const
	{
		return mIsOpen;
	}

private:
	EventLoop::EventLoop& mEv;
	bool mIsOpen{false};
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
