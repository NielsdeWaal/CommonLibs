#ifndef __URINGCOMMANDS_H_
#define __URINGCOMMANDS_H_

#include <cstdint>
#include <liburing/io_uring.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace EventLoop {

// class CompletionQueueEvent
// {
// };

// class SubmissionQueueEvent
// {
// };

using CompletionQueueEvent = io_uring_cqe;
using SubmissionQueueEvent = io_uring_sqe;

struct alignas(64) UserData;

/**
 * @brief Callback handler for io_uring completion events.
 */
class IUringCallbackHandler
{
public:
	virtual void OnCompletion(CompletionQueueEvent& cqe, const UserData*) = 0;
	virtual ~IUringCallbackHandler() = default;
	// IUringCallbackHandler(const IUringCallbackHandler&) = delete;
	// IUringCallbackHandler(const IUringCallbackHandler&&) = delete;
	// IUringCallbackHandler& operator=(const IUringCallbackHandler&) = delete;
	// IUringCallbackHandler& operator=(const IUringCallbackHandler&&) = delete;
};

/**
 * @brief Small class marker for io_uring commands
 */
struct UringCommandMarker
{
};

/**
 * @brief
 */
enum class SourceType : std::uint8_t
{
	Write = 1,
	Read = 2,
	SockSend = 3,
	SockRecv = 4,
	SockRecvMsg = 5,
	SockSendMsg = 6,
	Open = 7,
	FdataSync = 8,
	Fallocate = 9,
	Close = 10,
	LinkRings = 11,
	Statx = 12,
	Timeout = 13,
	Connect = 14,
	Accept = 15,
	Nop = 16,
	Invalid = 0,
};

// std::string GetUringType(const SourceType& type)
// {
// 	switch(type)
// 	{
// 	case SourceType::Write:
// 		return "Write";
// 	case SourceType::Read:
// 		return "Read";
// 	case SourceType::SockSend:
// 		return "SockSend";
// 	case SourceType::SockRecv:
// 		return "SockRecv";
// 	case SourceType::SockRecvMsg:
// 		return "SockRecvMsg";
// 	case SourceType::SockSendMsg:
// 		return "SockSendMsg";
// 	case SourceType::Open:
// 		return "Open";
// 	case SourceType::FdataSync:
// 		return "FdataSync";
// 	case SourceType::Fallocate:
// 		return "Fallocate";
// 	case SourceType::Close:
// 		return "Close";
// 	case SourceType::LinkRings:
// 		return "LinkRings";
// 	case SourceType::Statx:
// 		return "Statx";
// 	case SourceType::Timeout:
// 		return "Timeout";
// 	case SourceType::Connect:
// 		return "Connect";
// 	case SourceType::Accept:
// 		return "Accept";
// 	case SourceType::Nop:
// 		return "Nop";
// 	case SourceType::Invalid:
// 		return "Invalid";
// 	default:
// 		throw std::runtime_error("Invalid uring type");
// 	}
// }

// struct Write : public UringCommandMarker
// {
// 	std::vector<std::uint8_t> mIOBuffer;
// };

struct NOP : public UringCommandMarker
{
};

struct CLOSE : public UringCommandMarker
{
	int fd;
};

struct OPEN : public UringCommandMarker
{
	std::string* filename;
	int flags;
	int mode;
};

struct READ : public UringCommandMarker
{
	int fd;
	void* buf;
	std::size_t len;
	std::uint64_t pos;
};

struct WRITE : public UringCommandMarker
{
	int fd;
	void* buf;
	std::size_t len;
	std::uint64_t pos;
};

using UringCommand = std::variant<NOP, OPEN, CLOSE, READ, WRITE>;

// FIXME We want to make sure that the request type inherits from the marker
// template<class RequestData, typename std::enable_if_t<std::is_base_of_v<UringCommandMarker, RequestData>>>
// FIXME We cannot use templates to determine the type of RequestData.
// This type information gets lost when we cast back from the void* of the cqe.
// Possibly switch on mType.
// template<class RequestData>
struct alignas(64) UserData
{
	IUringCallbackHandler* mCallback = nullptr;
	SourceType mType = SourceType::Invalid;
	UringCommand mInfo;
	// RequestData mData;
};

} // namespace EventLoop

#endif // __URINGCOMMANDS_H_
