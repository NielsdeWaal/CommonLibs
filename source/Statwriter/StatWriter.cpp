#include "StatWriter.h"

namespace StatWriter {

void StatWriter::AddGroup(const std::string& label, const bool batch)
{ }

void StatWriter::AddFieldToGroup(const std::string& label, const std::function<int()> getter)
{ }

void StatWriter::WriteBatch()
{ }

void StatWriter::AddMeasurementsToLine(InfluxDBLine& line, const std::string& label)
{
	const auto& currentBatch = mBatchMeasurements[label].second;
	for(const auto& metric : currentBatch)
	{
		mFieldSet += std::string{metric()} + ",";
	}

	mFieldSet.pop_back();
}

}
