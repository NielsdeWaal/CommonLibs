#ifndef NONCOPYABLE_H
#define NONCOPYABLE_H

namespace Common {

template<typename Derived = void>
class NonCopyable 
{
public:
	/**
	 * Default constructor, does nothing.
	 */
	NonCopyable () = default;

	/**
	 * Deleted copy-constructor.
	 */
	NonCopyable (NonCopyable const &) = delete;

	/**
	 * Deleted copy-assignment operator.
	 */
	NonCopyable & operator= (NonCopyable const &) = delete;
};

}

#endif // NONCOPYABLE_H
