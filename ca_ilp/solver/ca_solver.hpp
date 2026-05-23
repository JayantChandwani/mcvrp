#pragma once

#include "../utils/types.hpp"
#include <map>

namespace mcvrp::ca::solver {

BidResult generate_bids_matrix(
    const Dataset& dataset,
    int max_subset_size = 3,
    int capacity = 100,
    int neighbor_limit = 12,
    int max_candidates = 250000
);

std::map<int, BestBid> find_best_bids(
    const std::vector<std::vector<double>>& bid_matrix,
    const std::vector<std::vector<int>>& subsets,
    const std::vector<std::vector<int>>& subset_orders,
    const std::vector<int>& feasible
);

SolutionResult solve_set_partitioning(
    const std::map<int, BestBid>& best_bids,
    int n_targets
);

DatasetResult process_dataset(
    int id,
    const Dataset& dataset,
    int max_subset_size = 3,
    int capacity = 100,
    int neighbor_limit = 12,
    int max_candidates = 250000
);

} // namespace mcvrp::ca::solver
