#ifndef __DMA_BUFFER_H
#define __DMA_BUFFER_H

#include <cstdlib>
#include <stdexcept>
namespace EventLoop {

class DmaBuffer
{
public:
	DmaBuffer(std::size_t size)
	{
		mBuf = std::aligned_alloc(512, size);
		if(!mBuf)
		{
			throw std::runtime_error("Failed to allocated aligned memory");
		}
	}

	~DmaBuffer()
	{
		if(mBuf)
		{
			std::free(mBuf);
		}
	}

	[[nodiscard]] void* GetPtr() const {
		return mBuf;
	}

	[[nodiscard]] std::size_t GetSize() const {
		return mSize;
	}

private:
	void* mBuf{nullptr};
	std::size_t mSize;
};

} // namespace EventLoop

#endif // DmaBuffer_H
