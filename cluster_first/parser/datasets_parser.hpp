#pragma once
#include <string>
#include <vector>
#include <utility>
#include "../utils/types.hpp"

namespace mcvrp::parser {

struct Dataset {
	int index = 0;
	std::vector<mcvrp::types::Coordinates> depots;
	std::vector<mcvrp::types::Coordinates> targets;
	std::vector<int> weights;
};

// Parses a datasets.txt file (multiple datasets) and returns a vector of
// (name, GraphInput) pairs.
std::vector<std::pair<std::string, mcvrp::types::GraphInput>> parse_datasets_txt(const std::string& filepath);

// Strict parser that mirrors the original Python preprocessing logic.
std::vector<Dataset> parse_datasets_txt_raw(const std::string& filepath);

// Build per-depot cluster graphs for a dataset using nearest-depot assignment.
std::vector<std::pair<std::string, mcvrp::types::GraphInput>> build_cluster_graphs(
	const Dataset& dataset,
	bool keep_empty
);

} // namespace mcvrp::parser
