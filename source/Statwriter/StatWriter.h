#ifndef STATWRITER_H
#define STATWRITER_H

#include <spdlog/fmt/ostr.h>

#include "EventLoop.h"
#include "TSC.h"
#include "UDPSocket.h"

#include <chrono>
#include <variant>

namespace StatWriter {

template<class... Ts>
struct overloaded : Ts...
{
	using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

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
		friend OStream& operator<<(OStream& os, const InfluxDBLine& l)
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

			ret += " ";
			ret += std::to_string(
				std::chrono::duration_cast<std::chrono::nanoseconds>(mTimestamp.time_since_epoch()).count());

			return ret;
		}
	};

public:
	StatWriter(EventLoop::EventLoop& ev)
		: mEventLoop(ev)
		, mSocket(ev, this)
	{
		mLogger = mEventLoop.RegisterLogger("StatWriter");
	}

	void InfluxConnector(const std::string& addr, const uint16_t port) noexcept
	{
		mServerAddress = addr;
		mServerPort = port;
	}

	void SetBatchWriting(Common::MONOTONIC_TIME interval) noexcept
	{
		if(mTimerSet)
		{
			mLogger->warn("Changing timer interval not supported atm, skipping call");
			return;
		}
		mBatchInterval = interval;

		mTimer = EventLoop::EventLoop::Timer(interval, EventLoop::EventLoop::TimerType::Repeating, [this] {
			WriteBatch();
		});

		mEventLoop.AddTimer(&mTimer);
		mTimerSet = true;
	}

	void AddMultipleToGroupInBatch(const std::string& group,
		const std::unordered_map<std::string, std::function<std::variant<int, float>()>>& getters) noexcept
	{
		for(const auto& [label, getter]: getters)
		{
			mBatchMeasurements[group][label] = getter;
			mLogger->info("Added field: {} to group: {}", label, group);
		}
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
	void AddFieldToGroup(const std::string& group, const std::string& label,
		const std::function<std::variant<int, float>()> getter) noexcept;

	/**
	 * @brief Sends state directly to TSDB
	 */
	void SendFieldAndGroupImidiate(
		const std::string& group, const std::string& label, const std::variant<int, float>& value) const noexcept
	{
		InfluxDBLine line;

		line.mTimestamp = std::chrono::high_resolution_clock::now();
		line.mMeasurement = group;
		std::visit(overloaded{
					   [&line, &label](int arg) {
						   line.mFieldSet = label + "=" + std::to_string(arg);
					   },
					   [&line, &label](float arg) {
						   line.mFieldSet = label + "=" + std::to_string(arg);
					   },
				   },
			value);

		const auto data = line.GetLine();
		mLogger->info("Sending value: {} to {} now", line.mFieldSet, group);
		mSocket.Send(data.c_str(), data.size(), mServerAddress.c_str(), mServerPort);
	}

	/**
	 * @brief Send values directly to TSDB
	 */
	void SendFieldAndGroupImidiate(const std::string& group,
		const std::unordered_map<std::string, std::variant<int, float>>& values) const noexcept
	{
		InfluxDBLine line;

		line.mTimestamp = std::chrono::high_resolution_clock::now();
		line.mMeasurement = group;
		std::vector<std::string> formattedValues;
		for(const auto& [metric, value]: values)
		{
			std::string metricValue = "";
			std::visit(overloaded{
						   [&metricValue](int arg) {
							   metricValue = std::to_string(arg);
						   },
						   [&metricValue](float arg) {
							   metricValue = std::to_string(arg);
						   },
					   },
				value);
			formattedValues.push_back(metric + "=" + metricValue);
		}
		line.mFieldSet = fmt::format("{}", fmt::join(formattedValues, ","));
	}

private:
	/**
	 * @brief Sends batch of values to TSDB
	 */
	void WriteBatch() noexcept;

	/**
	 * @brief Retrieve fields from registered getter and add these to influx line
	 */
	void AddMeasurementsToLine(InfluxDBLine& line, const std::string& group)
	{
		const auto& currentBatch = mBatchMeasurements[group];
		std::vector<std::string> values;
		for(const auto& metric: currentBatch)
		{
			std::string metricValue = "";
			std::visit(overloaded{
						   [&metricValue](int arg) {
							   metricValue = std::to_string(arg);
						   },
						   [&metricValue](float arg) {
							   metricValue = std::to_string(arg);
						   },
					   },
				metric.second());
			values.push_back(metric.first + "=" + metricValue);
		}

		line.mFieldSet = fmt::format("{}", fmt::join(values, ","));
	}

	EventLoop::EventLoop& mEventLoop;
	EventLoop::EventLoop::Timer mTimer;
	Common::MONOTONIC_TIME mBatchInterval;
	bool mTimerSet = false;

	Common::UDPSocket mSocket;
	std::string mServerAddress;
	std::uint16_t mServerPort;

	// FIXME
	// Current implementation doesn't provide ordering for metrics written in line messages.
	// Is this even a thing that we want/require? No clue.
	std::unordered_map<std::string, std::unordered_map<std::string, std::function<std::variant<int, float>()>>>
		mBatchMeasurements;

	std::shared_ptr<spdlog::logger> mLogger;
};

} // namespace StatWriter

#endif // STATWRITER_H
