#ifndef STATWRITER_H
#define STATWRITER_H

#include <spdlog/fmt/ostr.h>

#include <EventLoop.h>
#include <UDPSocket.h>

#include <chrono>
#include <variant>

namespace StatWriter {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

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
				return os << l.mMeasurement << " " << l.mTagSet << " " << l.mFieldSet;
			}
			else
			{
				return os << l.mMeasurement << " " << l.mFieldSet;
			}
		}

		const std::string GetLine() const noexcept
		{
			std::string ret = "";

			ret += mMeasurement;
			ret += " ";
			if(mTagSet.size() != 0)
			{
				ret += mTagSet;
				ret += ",";
			}
			ret += mFieldSet;

			return ret;
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

	void InfluxConnector(const std::string& addr, const uint16_t port) noexcept
	{
		mServerAddress = addr;
		mServerPort = port;
	}

	void SetBatchWriting(std::chrono::seconds interval) noexcept
	{
		if(mTimerSet)
		{
			mLogger->warn("Changing timer interval not supported atm, skipping call");
			return;
		}
		mBatchInterval = interval;

		mTimer = EventLoop::EventLoop::Timer(interval,
					EventLoop::EventLoop::TimerType::Repeating,
					[this] { WriteBatch(); });

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
	 * - Template ref with name (e.g template<auto& var, string name>)
	 */
	void AddGroup(const std::string& label, const bool batch) noexcept;
	
	/**
	 * @brief Add field to group for reporting
	 *
	 * This function adds a getter to storage for usage when reporting values.
	 *
	 * @issue The issue with the current setup is that it only excepts integers returns.
	 * This should be fixable with variants.
	 */
	void AddFieldToGroup(const std::string& group,
						const std::string& label,
						const std::function<std::variant<int,float>()> getter) noexcept;

private:
	/**
	 * @brief Sends batch of values to TSDB
	 */
	void WriteBatch() noexcept;

	/**
	 * @brief Retrieve fields from registered getter and add these to influx line
	 *
	 * @todo current implementation uses ugly loop for joining variables.
	 * This should be replaced with an FMT (join?) function
	 */
	void AddMeasurementsToLine(InfluxDBLine& line, const std::string& group)
	{
		const auto& currentBatch = mBatchMeasurements[group];
		for(const auto& metric : currentBatch)
		{
			std::string metricValue = "";
			std::visit(overloaded {
					[&metricValue](int arg) { metricValue = std::to_string(arg); },
					[&metricValue](float arg) { metricValue = std::to_string(arg); },
					}, metric.second());
			//line.mFieldSet += metric.first + "=" + std::to_string(metric.second()) + ","s;
			line.mFieldSet += metric.first + "=" + metricValue + ","s;
		}

		line.mFieldSet.pop_back();
	}

	EventLoop::EventLoop& mEventLoop;
	EventLoop::EventLoop::Timer mTimer;
	std::chrono::seconds mBatchInterval;
	bool mTimerSet = false;

	Common::UDPSocket mSocket;
	std::string mServerAddress;
	std::uint16_t mServerPort;

	//FIXME
	//Current implementation doesn't provide ordering for metrics written in line messages.
	//Is this undesireable? No clue
	std::unordered_map<std::string,
			std::unordered_map<
				std::string, std::function<
					std::variant<int,float>()>>> mBatchMeasurements;

	std::shared_ptr<spdlog::logger> mLogger;
};

}

#endif // STATWRITER_H
