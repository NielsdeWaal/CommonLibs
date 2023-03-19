#ifndef __DMA_BUFFER_H
#define __DMA_BUFFER_H

#include "NonCopyable.h"
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
namespace EventLoop {

class DmaBuffer //: Common::NonCopyable<DmaBuffer>
{
public:
	DmaBuffer() = default;
	DmaBuffer(std::size_t size)
		: mSize(size)
	{
		mBuf = std::aligned_alloc(512, size);
		if(!mBuf)
		{
			throw std::runtime_error("Failed to allocated aligned memory");
		}
	}

	DmaBuffer(DmaBuffer&& buf)
		: mBuf(std::exchange(buf.mBuf, nullptr))
		, mSize(std::exchange(buf.mSize, 0))
	{}

	DmaBuffer& operator=(DmaBuffer&& buf)
	{
		mBuf = std::exchange(buf.mBuf, nullptr);
		mSize = std::exchange(buf.mSize, 0);
		return *this;
	}

	// DmaBuffer(DmaBuffer&) = delete;
	// DmaBuffer& operator=(const DmaBuffer&) = delete;

	DmaBuffer(DmaBuffer& buf)
		: mBuf(buf.mBuf)
		, mSize(buf.mSize)
	{
		buf.mBuf = nullptr;
		buf.mSize = 0;
	}

	// DmaBuffer& operator=(DmaBuffer& buf)
	// {
	// 	mBuf = std::exchange(buf.mBuf, nullptr);
	// 	mSize = std::exchange(buf.mSize, 0);
	// 	return *this;
	// }

	~DmaBuffer()
	{
		// if(mBuf)
		// {
		// 	std::free(mBuf);
		// }
	}

	[[nodiscard]] void* GetPtr() const
	{
		return mBuf;
	}

	[[nodiscard]] std::size_t GetSize() const
	{
		return mSize;
	}

	void TrimSize(std::size_t target)
	{
		assert(target <= mSize);
		mSize = target;
	}

	void Free()
	{
		if(mBuf)
		{
			std::free(mBuf);
			mSize = 0;
		}
	}

private:
	void* mBuf{nullptr};
	std::size_t mSize;
};

// static_assert(std::is_copy_constructible_v<DmaBuffer>, "Needs to be copy constructable");
static_assert(std::is_move_constructible_v<DmaBuffer>, "Needs move constructor");

} // namespace EventLoop

#endif // DmaBuffer_H
