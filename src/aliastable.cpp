#include "aliastable.h"

//This is actually such a cool data structure
std::vector<AliasTableEntry> buildAliasTable(const std::vector<double> &weights, std::vector<float> &pdf, float *totalWeight /*= nullptr*/) {
	size_t n = weights.size();
	double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
	if (totalWeight) *totalWeight = static_cast<float>(sum);
	if (sum == 0.0) return std::vector<AliasTableEntry>();

	std::vector<AliasTableEntry> table(n);
	std::vector<double> q;
	q.reserve(n);
	std::vector<size_t> under, over;
	pdf.resize(n);

	for (size_t i = 0; i < pdf.size(); ++i) {
		//Normalize by the sum
		pdf[i] = weights[i] / static_cast<float>(sum);

		double pHat = pdf[i] * static_cast<double>(n);
		if (pHat < 1.0) under.push_back(i);
		else over.push_back(i);

		q.push_back(pHat);
	}

	//Pair under elements with over. We sort of "fold" the over elements onto the under elements.
	//We get the threshold between picking over or under, where if we pick under, we use the index of the entry, and if we pick over
	//we use the alias of the entry.
	while (!under.empty() && !over.empty()) {
		size_t u = under.back();
		size_t o = over.back();

		under.pop_back();

		table[u] = { static_cast<float>(q[u]), static_cast<uint32_t>(o) };
		q[o] = q[o] + q[u] - 1.0;
		if (q[o] < 1.0) {
			over.pop_back();
			under.push_back(o);
		}
	}

	//Deal with the leftovers
	for (size_t u : under) table[u] = { 1.f, static_cast<uint32_t>(u) };
	for (size_t o : over) table[o] = { 1.f, static_cast<uint32_t>(o) };

	return table;
}