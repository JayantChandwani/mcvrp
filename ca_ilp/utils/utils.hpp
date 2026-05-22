#pragma once

#include "types.hpp"
#include <string>
#include <utility>
#include <vector>

namespace mcvrp::ca::utils {

std::string trim(const std::string& s);
std::pair<double, double> parse_coord(const std::string& token);

std::vector<std::vector<double>> build_dist_matrix(
    const std::vector<std::pair<double, double>>& vehicle_coords,
    const std::vector<std::pair<double, double>>& target_coords
);

std::vector<std::vector<double>> build_target_distance_matrix(
    const std::vector<std::pair<double, double>>& target_coords
);

std::vector<std::vector<int>> build_candidate_subsets(
    const std::vector<std::pair<double, double>>& target_coords,
    int max_subset_size = 3,
    int neighbor_limit = 12,
    int max_candidates = 250000
);

std::vector<SubsetTSP> precompute_tsp_cache(
    const std::vector<std::vector<int>>& subsets,
    const std::vector<std::vector<double>>& dist_matrix,
    int depot_count,
    const std::vector<int>& weights,
    int capacity = 100
);

} // namespace mcvrp::ca::utils
