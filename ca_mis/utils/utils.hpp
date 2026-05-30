#pragma once

#include "types.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mcvrp::ca_mis::utils {

// Dynamically-scheduled parallel loop over [begin, end) using std::thread
// (portable replacement for `#pragma omp parallel for schedule(dynamic)`).
template <class F>
inline void parallel_for(int begin, int end, int num_threads, F&& fn) {
    const int count = end - begin;
    if (count <= 0) return;
    num_threads = std::max(1, std::min(num_threads, count));
    if (num_threads == 1) {
        for (int i = begin; i < end; ++i) fn(i);
        return;
    }
    std::atomic<int> next(begin);
    std::vector<std::thread> pool;
    pool.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        pool.emplace_back([&]() {
            for (int i = next.fetch_add(1, std::memory_order_relaxed); i < end;
                 i = next.fetch_add(1, std::memory_order_relaxed)) {
                fn(i);
            }
        });
    }
    for (auto& th : pool) th.join();
}

std::string trim(const std::string& s);
std::pair<double, double> parse_coord(const std::string& token);

// Expand a "1,3,5-9" style selection into a sorted, de-duplicated id list.
std::vector<int> parse_dataset_selection(const std::string& raw);

// Write combined / per-dataset / final_output CSVs under ../output/<method>/<scenario>.
void write_results(
    const std::string& method,
    const std::string& scenario,
    const std::vector<DatasetResult>& results,
    const std::map<int, Dataset>& datasets
);

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

} // namespace mcvrp::ca_mis::utils
