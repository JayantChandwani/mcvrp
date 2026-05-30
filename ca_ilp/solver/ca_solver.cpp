#include "ca_solver.hpp"

#include "../utils/utils.hpp"
#include "ortools/linear_solver/linear_solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace mcvrp::ca::solver {

using operations_research::MPSolver;
using operations_research::MPVariable;

BidResult generate_bids_matrix(
    const Dataset& dataset,
    int max_subset_size,
    int capacity,
    int neighbor_limit,
    int max_candidates
) {
    const int v = static_cast<int>(dataset.vehicle_coords.size());
    const int t = static_cast<int>(dataset.target_coords.size());

    auto subsets = utils::build_candidate_subsets(
        dataset.target_coords,
        max_subset_size,
        neighbor_limit,
        max_candidates
    );
    const int s_count = static_cast<int>(subsets.size());

    auto dist_matrix = utils::build_dist_matrix(dataset.vehicle_coords, dataset.target_coords);
    auto tsp_cache = utils::precompute_tsp_cache(subsets, dist_matrix, v, dataset.weights, capacity);

    std::vector<int> feasible;
    feasible.reserve(s_count);
    for (int s = 0; s < s_count; ++s) {
        if (tsp_cache[s].feasible) feasible.push_back(s);
    }

    const int f_count = static_cast<int>(feasible.size());
    std::vector<std::vector<double>> bid_matrix(v, std::vector<double>(s_count, std::numeric_limits<double>::infinity()));
    std::vector<std::vector<int>> subset_orders(s_count);
    for (int s = 0; s < s_count; ++s) {
        if (tsp_cache[s].feasible) {
            subset_orders[s] = tsp_cache[s].order;
        }
    }

    for (int d = 0; d < v; ++d) {
        for (int fi = 0; fi < f_count; ++fi) {
            const int s = feasible[fi];
            const auto& tc = tsp_cache[s];
            bid_matrix[d][s] = dist_matrix[d][tc.first_idx]
                + tc.internal
                + dist_matrix[tc.last_idx][d];
        }
    }

    return {bid_matrix, subsets, feasible, subset_orders};
}

std::map<int, BestBid> find_best_bids(
    const std::vector<std::vector<double>>& bid_matrix,
    const std::vector<std::vector<int>>& subsets,
    const std::vector<std::vector<int>>& subset_orders,
    const std::vector<int>& feasible
) {
    std::map<int, BestBid> best;
    const int v = static_cast<int>(bid_matrix.size());

    for (int s : feasible) {
        double best_value = std::numeric_limits<double>::infinity();
        int best_depot = -1;
        for (int d = 0; d < v; ++d) {
            if (bid_matrix[d][s] < best_value) {
                best_value = bid_matrix[d][s];
                best_depot = d;
            }
        }
        if (best_value < std::numeric_limits<double>::infinity()) {
            BestBid bid;
            bid.depot = best_depot;
            bid.bid = best_value;
            bid.subset = subsets[s];
            bid.order = subset_orders[s].empty() ? subsets[s] : subset_orders[s];
            best[s] = std::move(bid);
        }
    }

    return best;
}

SolutionResult solve_set_partitioning(
    const std::map<int, BestBid>& best_bids,
    int n_targets
) {
    MPSolver solver("SPP", MPSolver::CBC_MIXED_INTEGER_PROGRAMMING);
    solver.SuppressOutput();

    std::unordered_map<int, MPVariable*> x;
    for (const auto& [id, bid] : best_bids) {
        x[id] = solver.MakeBoolVar("x_" + std::to_string(id));
    }

    auto* obj = solver.MutableObjective();
    for (const auto& [id, bid] : best_bids) {
        obj->SetCoefficient(x[id], bid.bid);
    }
    obj->SetMinimization();

    std::vector<std::vector<int>> target_map(n_targets);
    for (const auto& [id, bid] : best_bids) {
        for (int t : bid.subset) target_map[t].push_back(id);
    }

    for (int t = 0; t < n_targets; ++t) {
        if (target_map[t].empty()) return {{}, 0.0, "Uncoverable"};
        auto* c = solver.MakeRowConstraint(1, 1);
        for (int s : target_map[t]) c->SetCoefficient(x[s], 1);
    }

    if (solver.Solve() == MPSolver::OPTIMAL) {
        std::vector<BestBid> sol;
        double cost = 0.0;
        for (const auto& [id, bid] : best_bids) {
            if (x[id]->solution_value() > 0.5) {
                sol.push_back(bid);
                cost += bid.bid;
            }
        }
        return {sol, cost, "Optimal"};
    }

    return {{}, 0.0, "Failed"};
}

DatasetResult process_dataset(
    int id,
    const Dataset& dataset,
    int max_subset_size,
    int capacity,
    int neighbor_limit,
    int max_candidates
) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    auto bids = generate_bids_matrix(dataset, max_subset_size, capacity, neighbor_limit, max_candidates);
    auto best = find_best_bids(bids.bid_matrix, bids.subsets, bids.subset_orders, bids.feasible_subsets);
    auto sol = solve_set_partitioning(best, static_cast<int>(dataset.target_coords.size()));
    const double elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
    DatasetResult result;
    result.id = id;
    result.status = sol.status;
    result.cost = sol.total_cost;
    result.time_sec = elapsed;
    result.feasible_subsets = static_cast<int>(bids.feasible_subsets.size());
    result.total_subsets = static_cast<int>(bids.subsets.size());
    result.selected = sol.selected;
    return result;
}

} // namespace mcvrp::ca::solver
