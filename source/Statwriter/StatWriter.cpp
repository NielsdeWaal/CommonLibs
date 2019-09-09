#include "StatWriter.h"

namespace StatWriter {

void StatWriter::AddGroup(const std::string& label, const bool batch)
{ }

void StatWriter::AddFieldToGroup(const std::string& label, const std::function<int()> getter)
{ }

void StatWriter::WriteBatch()
{ }

}
