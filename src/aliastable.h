#pragma once
#include <vector>
#include <numeric>
#include <cstdint>

struct AliasTableEntry {
	float threshold;
	uint32_t alias;
};

//TODO: Put this on the GPU or multithread it?
std::vector<AliasTableEntry> buildAliasTable(const std::vector<double> &weights, std::vector<float> &pdf, float *totalWeight = nullptr);