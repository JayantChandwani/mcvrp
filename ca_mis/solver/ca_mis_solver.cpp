#include "ca_mis_solver.hpp"

#include "../utils/utils.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <random>

namespace mcvrp::ca_mis::solver {

CandidateBids compute_best_bids(
    const Dataset& dataset,
    int max_subset_size,
    int capacity,
    int neighbor_limit,
    int max_candidates
) {
    const int v = static_cast<int>(dataset.vehicle_coords.size());

    auto subsets = utils::build_candidate_subsets(
        dataset.target_coords,
        max_subset_size,
        neighbor_limit,
        max_candidates
    );
    const int s_count = static_cast<int>(subsets.size());

    auto dist_matrix = utils::build_dist_matrix(dataset.vehicle_coords, dataset.target_coords);
    auto tsp_cache = utils::precompute_tsp_cache(subsets, dist_matrix, v, dataset.weights, capacity);

    CandidateBids out;
    out.total_subsets = s_count;

    out.bids.reserve(s_count);
    for (int s = 0; s < s_count; ++s) {
        if (!tsp_cache[s].feasible) continue;
        ++out.feasible_subsets;

        const auto& tc = tsp_cache[s];
        double best_cost = std::numeric_limits<double>::infinity();
        int best_depot = -1;
        for (int d = 0; d < v; ++d) {
            const double cost = dist_matrix[d][tc.first_idx] + tc.internal + dist_matrix[tc.last_idx][d];
            if (cost < best_cost) {
                best_cost = cost;
                best_depot = d;
            }
        }

        if (best_depot >= 0 && best_cost < std::numeric_limits<double>::infinity()) {
            BestBid bid;
            bid.depot = best_depot;
            bid.bid = best_cost;
            bid.subset = subsets[s];
            bid.order = tc.order.empty() ? subsets[s] : tc.order;
            out.bids.push_back(std::move(bid));
        }
    }

    return out;
}

static std::pair<std::vector<int>, double> greedy_mis(
    const std::vector<BestBid>& bids,
    const std::vector<int>& order,
    int n_targets
) {
    const int n = static_cast<int>(bids.size());
    std::vector<int> target_owner(n_targets, -1);
    std::vector<bool> selected(n, false);
    std::vector<int> sel;

    for (int idx : order) {
        bool conflict = false;
        for (int t : bids[idx].subset) {
            if (target_owner[t] != -1) {
                conflict = true;
                break;
            }
        }
        if (conflict) continue;
        selected[idx] = true;
        sel.push_back(idx);
        for (int t : bids[idx].subset) target_owner[t] = idx;
    }

    for (int t = 0; t < n_targets; ++t) {
        if (target_owner[t] != -1) continue;
        double best_cost = std::numeric_limits<double>::infinity();
        int best_idx = -1;
        for (int j = 0; j < n; ++j) {
            if (selected[j]) continue;
            if (bids[j].bid >= best_cost) continue;
            bool has_t = false;
            for (int tt : bids[j].subset) {
                if (tt == t) {
                    has_t = true;
                    break;
                }
            }
            if (!has_t) continue;
            bool conflict = false;
            for (int tt : bids[j].subset) {
                if (target_owner[tt] != -1) {
                    conflict = true;
                    break;
                }
            }
            if (!conflict) {
                best_cost = bids[j].bid;
                best_idx = j;
            }
        }
        if (best_idx >= 0) {
            selected[best_idx] = true;
            sel.push_back(best_idx);
            for (int tt : bids[best_idx].subset) target_owner[tt] = best_idx;
        }
    }

    for (int t = 0; t < n_targets; ++t) {
        if (target_owner[t] == -1) return {{}, std::numeric_limits<double>::infinity()};
    }

    double cost = 0.0;
    for (int i : sel) cost += bids[i].bid;
    return {sel, cost};
}

static std::pair<std::vector<int>, double> local_search(
    const std::vector<BestBid>& bids,
    std::vector<int> sel,
    double cur_cost,
    int n_targets
) {
    const int n = static_cast<int>(bids.size());
    std::vector<int> target_owner(n_targets, -1);
    std::vector<bool> selected(n, false);
    for (int i : sel) {
        selected[i] = true;
        for (int t : bids[i].subset) target_owner[t] = i;
    }

    bool improved = true;
    while (improved) {
        improved = false;
        for (int& si : sel) {
            const int i = si;
            selected[i] = false;
            for (int t : bids[i].subset) target_owner[t] = -1;

            double best_cost = bids[i].bid;
            int best_j = -1;
            for (int j = 0; j < n; ++j) {
                if (selected[j] || j == i) continue;
                if (bids[j].bid >= best_cost) continue;
                if (bids[j].subset != bids[i].subset) continue;
                bool conflict = false;
                for (int t : bids[j].subset) {
                    if (target_owner[t] != -1) {
                        conflict = true;
                        break;
                    }
                }
                if (!conflict) {
                    best_cost = bids[j].bid;
                    best_j = j;
                }
            }

            const int use = (best_j >= 0) ? best_j : i;
            selected[use] = true;
            si = use;
            for (int t : bids[use].subset) target_owner[t] = use;
            if (best_j >= 0) {
                cur_cost = cur_cost - bids[i].bid + bids[best_j].bid;
                improved = true;
            }
        }
    }

    return {sel, cur_cost};
}

MISResult solve_mis(
    const std::vector<BestBid>& bids,
    int n_targets,
    int restarts
) {
    const int n = static_cast<int>(bids.size());
    if (n == 0) return {{}, 0.0, "Failed"};

    std::vector<int> base(n);
    std::iota(base.begin(), base.end(), 0);
    std::sort(base.begin(), base.end(), [&](int a, int b) {
        return (bids[a].bid / bids[a].subset.size()) < (bids[b].bid / bids[b].subset.size());
    });

    double best_cost = std::numeric_limits<double>::infinity();
    std::vector<int> best_sel;
    std::mt19937 rng(42);

    for (int r = 0; r < restarts; ++r) {
        std::vector<int> order = base;
        if (r > 0) {
            const int top = std::max(1, static_cast<int>(n * 0.3));
            std::shuffle(order.begin(), order.begin() + top, rng);
        }

        auto [sel, cost] = greedy_mis(bids, order, n_targets);
        if (sel.empty()) continue;
        auto [lsel, lcost] = local_search(bids, sel, cost, n_targets);
        if (lcost < best_cost) {
            best_cost = lcost;
            best_sel = std::move(lsel);
        }
    }

    if (best_cost >= 1e17 || best_sel.empty()) {
        return {{}, 0.0, "Failed"};
    }

    MISResult result;
    result.total_cost = best_cost;
    result.status = "Optimal";
    result.selected.reserve(best_sel.size());
    for (int idx : best_sel) {
        result.selected.push_back(bids[idx]);
    }
    return result;
}

DatasetResult process_dataset(
    int id,
    const Dataset& dataset,
    int max_subset_size,
    int capacity,
    int neighbor_limit,
    int max_candidates,
    int restarts
) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    auto bids = compute_best_bids(dataset, max_subset_size, capacity, neighbor_limit, max_candidates);
    auto sol = solve_mis(bids.bids, static_cast<int>(dataset.target_coords.size()), restarts);
    const double elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

    DatasetResult result;
    result.id = id;
    result.cost = sol.total_cost;
    result.time_sec = elapsed;
    result.status = sol.status;
    result.feasible_subsets = bids.feasible_subsets;
    result.total_subsets = bids.total_subsets;
    result.selected = sol.selected;
    return result;
}

} // namespace mcvrp::ca_mis::solver
