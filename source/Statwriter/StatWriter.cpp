#include "StatWriter.h"

namespace StatWriter {

void StatWriter::AddGroup(const std::string& label, const bool batch)
{
	if(batch)
	{
		mBatchMeasurements[label];
		mLogger->info("Added group: {} to batch writer", label);
	}
}

void StatWriter::AddFieldToGroup(const std::string& group,
	const std::string& label,
	const std::function<std::variant<int,float>()> getter)
{
	mBatchMeasurements[group][label] = getter;
	mLogger->info("Added field: {} to group: {}", label, group);
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
