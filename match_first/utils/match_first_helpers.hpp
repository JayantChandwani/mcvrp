#pragma once

#include "../parser/datasets_parser.hpp"
#include "types.hpp"

#include <vector>

namespace mcvrp::match_first {

using Group = std::vector<int>;

types::GraphInput build_target_graph(const parser::Dataset& dataset);

int scenario2_capacity(const parser::Dataset& dataset);
int scenario3_capacity(const parser::Dataset& dataset);

types::Coordinates centroid_of(const Group& group, const parser::Dataset& dataset);
int nearest_depot_id(const types::Coordinates& point, const parser::Dataset& dataset);

std::vector<Group> build_matched_groups(
    const parser::Dataset& dataset,
    const types::MatchingResult& matching
);

long long route_distance(const Group& group, int depot_id, const parser::Dataset& dataset);
long long cluster_distance(const Group& ordered_cluster, const parser::Dataset& dataset, int depot_id);
Group build_greedy_cluster_order(const Group& cluster, const parser::Dataset& dataset, int depot_id);

std::vector<Group> build_scenario2_groups(const parser::Dataset& dataset, const types::GraphInput& input, int capacity_override = -1);

} // namespace mcvrp::match_first
