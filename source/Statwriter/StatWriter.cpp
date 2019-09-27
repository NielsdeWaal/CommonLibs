#include "StatWriter.h"

namespace StatWriter {

void StatWriter::AddGroup(const std::string& label, const bool batch)
{
	if(batch)
	{
		mBatchMeasurements[label];
	}
}

void StatWriter::AddFieldToGroup(const std::string& group, const std::string& label, const std::function<int()> getter)
{
	mBatchMeasurements[group][label] = getter;
}

void StatWriter::WriteBatch()
{
	mLogger->info("Writing batches to server");
	for(const auto& batch : mBatchMeasurements)
	{
		InfluxDBLine line;
		line.mTimestamp = std::chrono::high_resolution_clock::now();

		line.mMeasurement = batch.first;
		AddMeasurementsToLine(line, batch.first);
		const auto data = line.GetLine();
		mLogger->info("Sending {} to server", data);
		mSocket.Send(data.c_str(), data.size(), mServerAddress.c_str(), mServerPort);
		//mLogger->info("[DEBUG] {}", line);
	}
}

void StatWriter::AddMeasurementsToLine(InfluxDBLine& line, const std::string& group)
{
	const auto& currentBatch = mBatchMeasurements[group];
	for(const auto& metric : currentBatch)
	{
		line.mFieldSet += metric.first + "=" + std::to_string(metric.second()) + ","s;
	}

	line.mFieldSet.pop_back();
}

void StatWriter::DebugLineMessages()
{
	for(const auto& batch : mBatchMeasurements)
	{
		InfluxDBLine line;
		line.mTimestamp = std::chrono::high_resolution_clock::now();

		line.mMeasurement = batch.first;
		AddMeasurementsToLine(line, batch.first);
		mLogger->info("[DEBUG] {}", line);
	}
}

}
