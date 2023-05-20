#ifndef __URINGCOMMANDS_H_
#define __URINGCOMMANDS_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <coroutine>

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

enum class HandleType : std::uint8_t
{
	Standard = 0,
	Coroutine,
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

struct resolver
{
	virtual void resolve(int result) noexcept = 0;
};

struct SqeAwaitable;

// FIXME We want to make sure that the request type inherits from the marker
// template<class RequestData, typename std::enable_if_t<std::is_base_of_v<UringCommandMarker, RequestData>>>
// FIXME We cannot use templates to determine the type of RequestData.
// This type information gets lost when we cast back from the void* of the cqe.
// Possibly switch on mType.
// template<class RequestData>
struct alignas(64) UserData
{
	HandleType mHandleType{HandleType::Standard};
	IUringCallbackHandler* mCallback{nullptr};
	SourceType mType = SourceType::Invalid;
	UringCommand mInfo{NOP{}};
	resolver* mResolver{nullptr};
	SqeAwaitable* mAwaitable;
	// RequestData mData;
};

struct resume_resolver final : resolver
{
	friend struct SqeAwaitable;

	~resume_resolver() = default;

	void resolve(int res) noexcept override
	{
		this->result = res;
		handle.resume();
	}

private:
	std::coroutine_handle<> handle;
	int result = 0;
};
static_assert(std::is_trivially_destructible_v<resume_resolver>);

struct deferred_resolver final
	: resolver
	, public Common::NonCopyable<deferred_resolver>
{

	void resolve(int res) noexcept override
	{
		this->result = res;
	}

#ifndef NDEBUG
	~deferred_resolver()
	{
		assert(!!result && "deferred_resolver is destructed before it's resolved");
	}
#endif

	std::optional<int> result;
};

struct callback_resolver final : resolver
{
	callback_resolver(std::function<void(int result)>&& callback)
		: cb(std::move(callback))
	{}

	void resolve(int result) noexcept override
	{
		this->cb(result);
		delete this;
	}

private:
	std::function<void(int result)> cb;
};

struct SqeAwaitable
{
	// TODO: use cancel_token to implement cancellation
	explicit SqeAwaitable(io_uring_sqe* awaitable) noexcept
		: awaitable(awaitable)
	{}

	// User MUST keep resolver alive before the operation is finished
	// void set_deferred(deferred_resolver& resolver) {
	//     io_uring_sqe_set_data(sqe, &resolver);
	// }

	void SetupData(deferred_resolver& resolver)
	{
		UserData* data = new UserData{.mHandleType = HandleType::Coroutine, .mResolver = &resolver};
		io_uring_sqe_set_data(awaitable, data);
	}

	void SetCallback(std::function<void(int result)> callback)
	{
		// io_uring_sqe_set_data(sqe, new callback_resolver(std::move(cb)));
		UserData* data =
			new UserData{.mHandleType = HandleType::Coroutine, .mResolver = (resolver*)new callback_resolver(std::move(callback))};
		io_uring_sqe_set_data(awaitable, data);
	}

	auto operator co_await()
	{
		struct await_sqe
		{
			resume_resolver resolver{};
			io_uring_sqe* awaitableSqe;

			await_sqe(io_uring_sqe* awaitable)
				: awaitableSqe(awaitable)
			{}

			constexpr bool await_ready() const noexcept
			{
				return false;
			}

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				resolver.handle = handle;
				// io_uring_sqe_set_data(sqe, &resolver);
				UserData* data = new UserData{.mHandleType = HandleType::Coroutine, .mResolver = &resolver};
				io_uring_sqe_set_data(awaitableSqe, data);
			}

			constexpr int await_resume() const noexcept
			{
				return resolver.result;
				// return 0;
			}
		};

		return await_sqe(awaitable);
	}

private:
	// EventLoop::EventLoop& mEv;
	// EventLoop& mEv;
	io_uring_sqe* awaitable;
	HandleType mType{HandleType::Coroutine};
	std::optional<int> mResult;
};

namespace uio {
template<typename T, bool nothrow>
struct task;

// only for internal usage
template<typename T, bool nothrow>
struct task_promise_base
{
	task<T, nothrow> get_return_object();
	auto initial_suspend()
	{
		return std::suspend_never();
	}
	auto final_suspend() noexcept
	{
		struct Awaiter : std::suspend_always
		{
			task_promise_base* me_;

			Awaiter(task_promise_base* me)
				: me_(me){};
			std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) const noexcept
			{
				if(__builtin_expect(me_->result_.index() == 3, false))
				{
					// FIXME: destroy current coroutine; otherwise memory leaks.
					if(me_->waiter_)
					{
						me_->waiter_.destroy();
					}
					std::coroutine_handle<task_promise_base>::from_promise(*me_).destroy();
				}
				else if(me_->waiter_)
				{
					return me_->waiter_;
				}
				return std::noop_coroutine();
			}
		};
		return Awaiter(this);
	}
	void unhandled_exception()
	{
		if constexpr(!nothrow)
		{
			if(__builtin_expect(result_.index() == 3, false))
				return;
			result_.template emplace<2>(std::current_exception());
		}
		else
		{
			__builtin_unreachable();
		}
	}

protected:
	friend struct task<T, nothrow>;
	task_promise_base() = default;
	std::coroutine_handle<> waiter_;
	std::variant<std::monostate, std::conditional_t<std::is_void_v<T>, std::monostate, T>,
		std::conditional_t<!nothrow, std::exception_ptr, std::monostate>,
		std::monostate // indicates that the promise is detached
		>
		result_;
};

// only for internal usage
template<typename T, bool nothrow>
struct task_promise final : task_promise_base<T, nothrow>
{
	using task_promise_base<T, nothrow>::result_;

	template<typename U>
	void return_value(U&& u)
	{
		if(__builtin_expect(result_.index() == 3, false))
			return;
		result_.template emplace<1>(static_cast<U&&>(u));
	}
	void return_value(int u)
	{
		if(__builtin_expect(result_.index() == 3, false))
			return;
		result_.template emplace<1>(u);
	}
};

template<bool nothrow>
struct task_promise<void, nothrow> final : task_promise_base<void, nothrow>
{
	using task_promise_base<void, nothrow>::result_;

	void return_void()
	{
		if(__builtin_expect(result_.index() == 3, false))
			return;
		result_.template emplace<1>(std::monostate{});
	}
};

// helper type for the visitor #4
template<class... Ts>
struct overloaded : Ts...
{
	using Ts::operator()...;
};

/**
 * An awaitable object that returned by an async function
 * @tparam T value type holded by this task
 * @tparam nothrow if true, the coroutine assigned by this task won't throw exceptions ( slightly better performance )
 * @warning do NOT discard this object when returned by some function, or UB WILL happen
 */
template<typename T = void, bool nothrow = false>
struct task final
{
	using promise_type = task_promise<T, nothrow>;
	using handle_t = std::coroutine_handle<promise_type>;

	task(const task&) = delete;
	task& operator=(const task&) = delete;

	bool await_ready()
	{
		auto& result_ = coro_.promise().result_;
		return result_.index() > 0;
	}

	template<typename T_, bool nothrow_>
	void await_suspend(std::coroutine_handle<task_promise<T_, nothrow_>> caller) noexcept
	{
		coro_.promise().waiter_ = caller;
	}

	T await_resume() const
	{
		return get_result();
	}

	/** Get the result held by this task */
	T get_result() const
	{
		auto& result_ = coro_.promise().result_;
		assert(result_.index() != 0);
		if constexpr(!nothrow)
		{
			if(auto* pep = std::get_if<2>(&result_))
			{
				std::rethrow_exception(*pep);
			}
		}
		if constexpr(!std::is_void_v<T>)
		{
			// FIXME this needs a special method to allow
			// for move-only types. The get_if function returns a ptr,
			// which in this case will call into the copy assignment
			// if constexpr(std::is_copy_constructible_v<T>) {
			return *std::get_if<1>(&result_);
			// } else {
			// 	return std::move(T{std::get<T>(std::move(coro_.promise().result_))});
			// T temp;
			// std::visit(overloaded {
			// 	[&temp](auto&& moved) {
			// 		temp = std::move(moved);
			// 	},
			// 	[](std::monostate) {
			// 		return;
			// 	},
			// 	[](std::exception_ptr) {
			// 		return;
			// 	}
			// },
			// 	result_);
			// return std::get_if<1>(coro_.promise().result_);
			// return std::get<T>(std::move(coro_.promise().result_));
			// return std::move(*std::get_if<1>(&result_));
			// }
		}
	}

	/** Get is the coroutine done */
	bool done() const
	{
		return coro_.done();
	}

	/** Only for placeholder */
	task()
		: coro_(nullptr){};

	task(task&& other) noexcept
	{
		coro_ = std::exchange(other.coro_, nullptr);
	}

	task& operator=(task&& other) noexcept
	{
		if(coro_)
			coro_.destroy();
		coro_ = std::exchange(other.coro_, nullptr);
		return *this;
	}

	/** Destroy (when done) or detach (when not done) the task object */
	~task()
	{
		if(!coro_)
			return;
		if(!coro_.done())
		{
			coro_.promise().result_.template emplace<3>(std::monostate{});
		}
		else
		{
			coro_.destroy();
		}
	}

private:
	friend struct task_promise_base<T, nothrow>;
	task(promise_type* p)
		: coro_(handle_t::from_promise(*p))
	{}
	handle_t coro_;
};

template<typename T, bool nothrow>
task<T, nothrow> task_promise_base<T, nothrow>::get_return_object()
{
	return task<T, nothrow>(static_cast<task_promise<T, nothrow>*>(this));
}

} // namespace uio

} // namespace EventLoop

#endif // __URINGCOMMANDS_H_
