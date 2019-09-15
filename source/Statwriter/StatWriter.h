#ifndef STATWRITER_H
#define STATWRITER_H

#include <spdlog/fmt/ostr.h>

#include <EventLoop.h>
#include <UDPSocket.h>

#include <chrono>

namespace StatWriter {

using namespace std::string_literals;

class StatWriter : public Common::IUDPSocketHandler
{
private:
	struct StatDefinition
	{
		std::string mLabel;
		std::function<int()> mStatFunction;
	};

	struct InfluxDBLine
	{
		std::string mMeasurement = "";
		std::string mTagSet = "";
		std::string mFieldSet = "";
		std::chrono::time_point<std::chrono::high_resolution_clock> mTimestamp;
		template<typename OStream>
		friend OStream &operator<<(OStream &os, const InfluxDBLine &l)
		{
			if(l.mTagSet != "")
			{
				return os << l.mMeasurement << " " << l.mTagSet << " " << l.mFieldSet; // << " " << l.mTimestamp;
			}
			else
			{
				return os << l.mMeasurement << " " << l.mFieldSet; // << " " << l.mTimestamp;
			}
		}
	};

public:
	StatWriter(EventLoop::EventLoop& ev)
		: mEventLoop(ev)
		, mSocket(ev, this)
	{
		mLogger = spdlog::get("StatWriter");
		if(mLogger == nullptr)
		{
			auto statWriterLogger = spdlog::stdout_color_mt("StatWriter");
			mLogger = spdlog::get("StatWriter");
		}
	}

	void DebugLineMessages();

	void InfluxConnector(const std::string& addr, const uint16_t port) noexcept
	{
		mSocket.Connect(addr.c_str(), port);
	}

	void SetBatchWriting(std::chrono::seconds interval) noexcept
	{
		if(mTimerSet)
		{
			mLogger->warn("Changing timer interval not supported atm, skipping call");
			return;
		}
		mBatchInterval = interval;
		mTimer = EventLoop::EventLoop::Timer(interval, EventLoop::EventLoop::TimerType::Repeating, [this] { WriteBatch(); });
		mEventLoop.AddTimer(&mTimer);
		mTimerSet = true;
	}

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
	 *   _metrics->add_group("cache", {sw::make_gauge("bytes", "used", [this] { return _region.occupancy().used_space(); })});
	 * }
	 * \endcode
	 *
	 */
	void AddGroup(const std::string& label, const bool batch);
	void AddFieldToGroup(const std::string& group, const std::string& label, const std::function<int()> getter);
	//void AddToBatch() noexcept

private:
	void WriteBatch();
	void AddMeasurementsToLine(InfluxDBLine& line, const std::string& group);

	EventLoop::EventLoop& mEventLoop;
	EventLoop::EventLoop::Timer mTimer;
	std::chrono::seconds mBatchInterval;
	bool mTimerSet;

	Common::UDPSocket mSocket;

	//FIXME
	//Current implementation doesn't provide ordering for metrics written in line messages.
	//Is this undesireable? No clue
	std::unordered_map<std::string, std::unordered_map<std::string, std::function<int()>>> mBatchMeasurements;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // STATWRITER_H
