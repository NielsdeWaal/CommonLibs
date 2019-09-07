#ifndef STATWRITER_H
#define STATWRITER_H

#include <EventLoop.h>
#include <UDPSocket.h>

namespace StatWriter {

class StatWriter : public Common::IUDPSocketHandler
{
public:
	StatWriter(EventLoop::EventLoop& ev)
		: mEventLoop(ev)
		, mSocket(ev, this)
	{
		auto streamSocketLogger = spdlog::stdout_color_mt("StatWriter");
		mLogger = spdlog::get("StatWriter");
	}

	void InfluxConnector(const std::string& addr, const uint16_t port) const noexcept
	{
		mSocket.Connect(addr.c_str(), port);
	}

	void SetBatchWriting(std::chrono::seconds interval) noexcept
	{}

	/**
	 * @brief Add variable to timed batch
	 *
	 * A variable is added to the list of items that get send to the TSDB at the specified interval.
	 *
	 * TODO Figure out a way to have a stable reference to variable in order to act like a singleton.
	 * We want to read from an external/independent class/variable but have the 
	 * safety of removing that reference when the data is out-of-use.
	 * IDEAS:
	 *	- Template ref with name (e.g template<auto& var, string name>)
	 *
	 * \code
	 * include "StatWriter.h"
	 *
	 * void A::setup_metrics() {
	 *   namespace sw = StatWriter::metrics;
	 *   _metrics = sw::create_metric_group();
	 *   _metrics->add_group("cache", {sm::make_gauge("bytes", "used", [this] { return _region.occupancy().used_space(); })});
	 * }
	 * \endcode
	 *
	 */
	//void AddToBatch() noexcept

private:
	EventLoop::EventLoop& mEventLoop;

	UDPSocket mSocket;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // STATWRITER_H
