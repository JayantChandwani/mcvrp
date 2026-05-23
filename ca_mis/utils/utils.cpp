#include "utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <omp.h>
#include <set>
#include <sstream>

namespace mcvrp::ca_mis::utils {

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

std::pair<double, double> parse_coord(const std::string& token) {
    const std::string t = trim(token);
    const std::size_t c = t.find(',');
    return {std::stod(t.substr(0, c)), std::stod(t.substr(c + 1))};
}

std::vector<std::vector<double>> build_dist_matrix(
    const std::vector<std::pair<double, double>>& vehicle_coords,
    const std::vector<std::pair<double, double>>& target_coords
) {
    const int v = static_cast<int>(vehicle_coords.size());
    const int t = static_cast<int>(target_coords.size());
    const int n = v + t;

    std::vector<std::pair<double, double>> all(n);
    for (int i = 0; i < v; ++i) all[i] = vehicle_coords[i];
    for (int i = 0; i < t; ++i) all[v + i] = target_coords[i];

    std::vector<std::vector<double>> dm(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const double dx = all[i].first - all[j].first;
            const double dy = all[i].second - all[j].second;
            const double d = std::sqrt(dx * dx + dy * dy);
            dm[i][j] = d;
            dm[j][i] = d;
        }
    }

    return dm;
}

std::vector<std::vector<double>> build_target_distance_matrix(
    const std::vector<std::pair<double, double>>& target_coords
) {
    const int n = static_cast<int>(target_coords.size());
    std::vector<std::vector<double>> dm(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const double dx = target_coords[i].first - target_coords[j].first;
            const double dy = target_coords[i].second - target_coords[j].second;
            const double d = std::sqrt(dx * dx + dy * dy);
            dm[i][j] = d;
            dm[j][i] = d;
        }
    }
    return dm;
}

static void comb_helper(
    int n,
    int sz,
    int start,
    std::vector<int>& cur,
    std::vector<std::vector<int>>& res
) {
    if (static_cast<int>(cur.size()) == sz) {
        res.push_back(cur);
        return;
    }
    for (int i = start; i < n; ++i) {
        cur.push_back(i);
        comb_helper(n, sz, i + 1, cur, res);
        cur.pop_back();
    }
}

static long long comb_count(int n, int k) {
    if (k < 0 || k > n) return 0;
    if (k == 0 || k == n) return 1;
    if (k > n - k) k = n - k;
    long long result = 1;
    for (int i = 1; i <= k; ++i) {
        result = (result * (n - k + i)) / i;
    }
    return result;
}

std::vector<std::vector<int>> build_candidate_subsets(
    const std::vector<std::pair<double, double>>& target_coords,
    int max_subset_size,
    int neighbor_limit,
    int max_candidates
) {
    const int n = static_cast<int>(target_coords.size());
    std::vector<std::vector<int>> subsets;

    const int upper = std::min(max_subset_size, n);
    for (int sz = 1; sz <= std::min(upper, 3); ++sz) {
        std::vector<int> cur;
        comb_helper(n, sz, 0, cur, subsets);
    }

    if (upper < 4) {
        return subsets;
    }

    const long long total_4 = comb_count(n, 4);
    if (total_4 <= max_candidates) {
        std::vector<int> cur;
        comb_helper(n, 4, 0, cur, subsets);
        return subsets;
    }

    const int limit = std::max(3, neighbor_limit);
    auto td = build_target_distance_matrix(target_coords);

    std::vector<std::vector<int>> nearest(n);
    for (int i = 0; i < n; ++i) {
        std::vector<int> order(n);
        for (int j = 0; j < n; ++j) order[j] = j;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (td[i][a] != td[i][b]) return td[i][a] < td[i][b];
            return a < b;
        });

        std::vector<int> neigh;
        neigh.reserve(limit);
        for (int idx : order) {
            if (idx == i || idx <= i) continue;
            neigh.push_back(idx);
            if (static_cast<int>(neigh.size()) >= limit) break;
        }
        nearest[i] = std::move(neigh);
    }

    std::set<std::array<int, 4>> chosen;
    for (int i = 0; i < n && static_cast<int>(chosen.size()) < max_candidates; ++i) {
        const auto& neigh = nearest[i];
        if (neigh.size() < 3) continue;
        const int m = static_cast<int>(neigh.size());
        for (int a = 0; a < m - 2; ++a) {
            for (int b = a + 1; b < m - 1; ++b) {
                for (int c = b + 1; c < m; ++c) {
                    chosen.insert({i, neigh[a], neigh[b], neigh[c]});
                    if (static_cast<int>(chosen.size()) >= max_candidates) break;
                }
                if (static_cast<int>(chosen.size()) >= max_candidates) break;
            }
            if (static_cast<int>(chosen.size()) >= max_candidates) break;
        }
    }

    for (const auto& item : chosen) {
        subsets.push_back({item[0], item[1], item[2], item[3]});
    }

    return subsets;
}

std::vector<SubsetTSP> precompute_tsp_cache(
    const std::vector<std::vector<int>>& subsets,
    const std::vector<std::vector<double>>& dist_matrix,
    int depot_count,
    const std::vector<int>& weights,
    int capacity
) {
    const int s_count = static_cast<int>(subsets.size());
    std::vector<SubsetTSP> cache(s_count);

    #pragma omp parallel for schedule(dynamic, 128)
    for (int s = 0; s < s_count; ++s) {
        const auto& sub = subsets[s];
        int total_weight = 0;
        for (int i : sub) total_weight += weights[i];
        if (total_weight > capacity) continue;

        cache[s].feasible = true;
        std::vector<int> abs(sub.size());
        for (std::size_t i = 0; i < sub.size(); ++i) {
            abs[i] = depot_count + sub[i];
        }

        const int n = static_cast<int>(abs.size());
        if (n == 1) {
            cache[s].first_idx = abs[0];
            cache[s].last_idx = abs[0];
            cache[s].internal = 0.0;
            cache[s].order = {sub[0]};
            continue;
        }

        if (n == 2) {
            cache[s].first_idx = abs[0];
            cache[s].last_idx = abs[1];
            cache[s].internal = dist_matrix[abs[0]][abs[1]];
            cache[s].order = {sub[0], sub[1]};
            continue;
        }

        std::vector<int> perm = abs;
        std::sort(perm.begin(), perm.end());
        double best = 1e18;
        int best_first = perm[0];
        int best_last = perm[n - 1];
        std::vector<int> best_perm = perm;

        do {
            double cost = 0.0;
            for (int i = 0; i < n - 1; ++i) {
                cost += dist_matrix[perm[i]][perm[i + 1]];
            }
            if (cost < best) {
                best = cost;
                best_first = perm[0];
                best_last = perm[n - 1];
                best_perm = perm;
            }
        } while (std::next_permutation(perm.begin(), perm.end()));

        cache[s].internal = best;
        cache[s].first_idx = best_first;
        cache[s].last_idx = best_last;
        cache[s].order.clear();
        cache[s].order.reserve(best_perm.size());
        for (int abs_idx : best_perm) {
            cache[s].order.push_back(abs_idx - depot_count);
        }
    }

    return cache;
}

} // namespace mcvrp::ca_mis::utils
