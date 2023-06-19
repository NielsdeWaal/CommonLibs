#ifndef TOPOLOGY_H_
#define TOPOLOGY_H_

#include <cstddef>
// #include <spdlog/fmt/bundled/core.h>

#include <filesystem>
#include <iostream>
#include <spdlog/fmt/bundled/core.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace EventLoop {

namespace fs = std::filesystem;

struct CpuLocation
{
	std::size_t cpu;
	std::size_t core;
	std::size_t package;
	std::size_t numa;
};

static std::vector<std::size_t> ParseList(std::string list)
{
	std::istringstream input{list};
	std::vector<std::size_t> res{};
	for(std::string unit; std::getline(input, unit, ',');)
	{
		const auto pos = unit.find('-');

		if(pos != std::string::npos)
		{
			std::size_t start = std::stoul(unit.substr(0, pos));
			std::size_t end = std::stoul(unit.substr(pos + 1, unit.size()));
			for(std::size_t i = start; i <= end; ++i)
			{
				res.push_back(i);
			}
		}
		else
		{
			res.push_back(std::stoul(unit));
		}
	}

	return res;
}

static std::string GetFileContent(const fs::path& path)
{
	std::ifstream f(path);

	std::ostringstream read;
	read << f.rdbuf();
	f.close();

	return read.str();
}

static std::size_t GetCoreId(
	const fs::path& basePath, std::size_t cpu, std::unordered_map<std::size_t, std::size_t>& cpuCoreMap)
{
	const auto siblings = ParseList(GetFileContent(basePath / "core_cpus_list"));
	if(cpuCoreMap.contains(cpu))
	{
		return cpuCoreMap[cpu];
	}

	const std::size_t core = ParseList(GetFileContent(basePath / "core_id")).at(0);

	for(const auto& sibling: siblings)
	{
		cpuCoreMap[sibling] = core;
	}

	return core;
}

static CpuLocation BuildCpuLocation(const fs::path& basePath, std::size_t cpu, std::size_t node,
	std::unordered_map<std::size_t, std::size_t>& cpuCoreMap)
{
	// basePath += fs::path(fmt::format("cpu/cpu{}/topology"));
	const auto cpuPath = basePath / fmt::format("cpu/cpu{}/topology", cpu);

	const std::size_t physicalPackage = ParseList(GetFileContent(cpuPath / "physical_package_id")).at(0);
	const std::size_t core = GetCoreId(cpuPath, cpu, cpuCoreMap);

	return CpuLocation{cpu, core, physicalPackage, node};
}

static std::vector<CpuLocation> GetMachineTopology()
{
	fs::path base("/sys/devices/system");
	std::vector<CpuLocation> res;
	std::unordered_map<std::size_t, std::size_t> cpuToCore;

	auto cpusOnline = ParseList(GetFileContent(base / "cpu/online"));

	auto nodesOnline = ParseList(GetFileContent(base / "node/online"));

	for(const auto& node: nodesOnline)
	{
		auto nodesCpus = ParseList(GetFileContent(base / fmt::format("node/node{}/cpulist", node)));
		for(const auto& cpu: nodesCpus)
		{
			if(std::find(cpusOnline.begin(), cpusOnline.end(), cpu) == cpusOnline.end())
			{
				continue;
			}

			res.push_back(BuildCpuLocation(base, cpu, node, cpuToCore));
		}
	}

	return res;
}

} // namespace EventLoop

template<>
struct fmt::formatter<EventLoop::CpuLocation> : fmt::formatter<std::string>
{
	template<typename FormatContext>
	auto format(EventLoop::CpuLocation loc, FormatContext& ctx)
	{
		return format_to(
			ctx.out(), "[cpu: {}, core: {}, package: {}, numa: {}]", loc.cpu, loc.core, loc.package, loc.numa);
	}
};

#endif // TOPOLOGY_H_
