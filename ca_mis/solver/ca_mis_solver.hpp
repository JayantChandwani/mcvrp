#pragma once

#include "../utils/types.hpp"

namespace mcvrp::ca_mis::solver {

CandidateBids compute_best_bids(
    const Dataset& dataset,
    int max_subset_size = 3,
    int capacity = 100,
    int neighbor_limit = 12,
    int max_candidates = 250000
);

MISResult solve_mis(
    const std::vector<BestBid>& bids,
    int n_targets,
    int restarts = 8
);

DatasetResult process_dataset(
    int id,
    const Dataset& dataset,
    int max_subset_size = 3,
    int capacity = 100,
    int neighbor_limit = 12,
    int max_candidates = 250000,
    int restarts = 8
);

} // namespace mcvrp::ca_mis::solver
