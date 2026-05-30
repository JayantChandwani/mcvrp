#include "cluster_first_helpers.hpp"

#include "../solver/solver.hpp"
#include "utils.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace mcvrp::cluster_first {
namespace {

using types::GraphInput;
using types::MatchingResult;
using types::Node;

struct ClusterNode {
    int id = 0;
    int demand = 0;
    std::vector<int> members;
    types::Coordinates coordinates{};
};

GraphInput build_complete_graph_from_clusters(const std::vector<ClusterNode>& clusters) {
    GraphInput g;
    g.nodes.reserve(clusters.size());

    for (const auto& c : clusters) {
        g.nodes.push_back({c.id, c.demand, c.coordinates});
    }

    for (std::size_t i = 0; i < clusters.size(); ++i) {
        for (std::size_t j = i + 1; j < clusters.size(); ++j) {
            const int w = utils::euclidean_distance_2dp_scaled(
                clusters[i].coordinates,
                clusters[j].coordinates
            );
            g.edges.push_back({clusters[i].id, clusters[j].id, w});
        }
    }

    return g;
}

std::unordered_set<int> get_unmatched_ids(const std::vector<ClusterNode>& nodes, const MatchingResult& matching) {
    std::unordered_set<int> all_ids;
    std::unordered_set<int> matched_ids;
    all_ids.reserve(nodes.size());
    matched_ids.reserve(nodes.size());

    for (const auto& n : nodes) {
        all_ids.insert(n.id);
    }
    for (const auto& [u, v] : matching.matched_edges) {
        matched_ids.insert(u);
        matched_ids.insert(v);
    }

    std::unordered_set<int> unmatched;
    for (int id : all_ids) {
        if (!matched_ids.count(id)) {
            unmatched.insert(id);
        }
    }
    return unmatched;
}

std::vector<ClusterNode> build_lone_nodes(
    const std::vector<int>& lone_ids,
    const std::unordered_map<int, Node>& original_by_id
) {
    std::vector<ClusterNode> lone_nodes;
    lone_nodes.reserve(lone_ids.size());

    for (int id : lone_ids) {
        const auto& n = original_by_id.at(id);
        lone_nodes.push_back({id, n.demand, {id}, n.coordinates});
    }

    return lone_nodes;
}

int max_demand(const GraphInput& input) {
    int max_w = 0;
    for (const auto& n : input.nodes) {
        if (n.id != 0) {
            max_w = std::max(max_w, n.demand);
        }
    }
    return max_w;
}

} // namespace

int scenario2_capacity(const GraphInput& input) {
    return 2 * max_demand(input);
}

int scenario3_capacity(const GraphInput& input) {
    return 3 * max_demand(input) / 2;
}

std::vector<int> build_greedy_cluster_order(
    const std::vector<int>& cluster,
    const graph::Graph& base_graph,
    int depot_id
) {
    if (cluster.empty()) return {};
    if (cluster.size() == 1) return cluster;

    std::vector<int> unvisited = cluster;
    std::vector<int> ordered;
    ordered.reserve(cluster.size());

    auto cmp_from_depot = [&](int lhs, int rhs) {
        const int d_lhs = base_graph.distance_by_id(depot_id, lhs);
        const int d_rhs = base_graph.distance_by_id(depot_id, rhs);
        if (d_lhs != d_rhs) return d_lhs < d_rhs;
        return lhs < rhs;
    };

    auto start_it = std::min_element(unvisited.begin(), unvisited.end(), cmp_from_depot);
    int current = *start_it;
    ordered.push_back(current);
    unvisited.erase(start_it);

    std::sort(unvisited.begin(), unvisited.end());

    long long best_cost = std::numeric_limits<long long>::max();
    std::vector<int> best_tail = unvisited;

    do {
        long long cost = static_cast<long long>(base_graph.distance_by_id(depot_id, current));
        int prev = current;
        for (int node_id : unvisited) {
            cost += static_cast<long long>(base_graph.distance_by_id(prev, node_id));
            prev = node_id;
        }
        cost += static_cast<long long>(base_graph.distance_by_id(prev, depot_id));

        if (cost < best_cost || (cost == best_cost && unvisited < best_tail)) {
            best_cost = cost;
            best_tail = unvisited;
        }
    } while (std::next_permutation(unvisited.begin(), unvisited.end()));

    ordered.insert(ordered.end(), best_tail.begin(), best_tail.end());

    return ordered;
}

long long cluster_distance(
    const std::vector<int>& ordered_cluster,
    const graph::Graph& base_graph,
    int depot_id
) {
    if (ordered_cluster.empty()) return 0;

    long long total = static_cast<long long>(base_graph.distance_by_id(depot_id, ordered_cluster.front()));
    for (std::size_t i = 1; i < ordered_cluster.size(); ++i) {
        total += static_cast<long long>(base_graph.distance_by_id(ordered_cluster[i - 1], ordered_cluster[i]));
    }
    total += static_cast<long long>(base_graph.distance_by_id(ordered_cluster.back(), depot_id));

    return total;
}

std::vector<std::vector<int>> build_scenario2_clusters(const GraphInput& input, int capacity) {
    std::unordered_map<int, Node> original_by_id;
    original_by_id.reserve(input.nodes.size());
    for (const auto& n : input.nodes) {
        original_by_id[n.id] = n;
    }

    std::vector<int> lone_ids;
    lone_ids.reserve(input.nodes.size());
    for (const auto& n : input.nodes) {
        if (n.id == 0) continue; // exclude source from all matchings
        lone_ids.push_back(n.id);
    }

    std::vector<std::vector<int>> final_clusters;

    while (lone_ids.size() >= 2) {
        auto lone_vertices = build_lone_nodes(lone_ids, original_by_id);

        std::unordered_map<int, ClusterNode> lone_by_id;
        lone_by_id.reserve(lone_vertices.size());
        for (const auto& lv : lone_vertices) {
            lone_by_id[lv.id] = lv;
        }

        // Step 1: run matching on complete graph of current lone vertices.
        auto g1 = build_complete_graph_from_clusters(lone_vertices);
        if (g1.edges.empty()) {
            break;
        }

        graph::Graph G1(g1);
        solver::Solver solver;
        auto first_matching = solver.get_minimal_weighted_matching(G1);

        // Unmatched from first level remain lone for next cycle.
        std::unordered_set<int> next_lone_ids = get_unmatched_ids(lone_vertices, first_matching);

        // Step 2: build midpoint graph from matched pairs.
        std::vector<ClusterNode> pair_nodes;
        pair_nodes.reserve(first_matching.matched_edges.size());
        int next_midpoint_id = -1;

        for (const auto& [u, v] : first_matching.matched_edges) {
            const auto& a = lone_by_id.at(u);
            const auto& b = lone_by_id.at(v);

            ClusterNode m;
            m.id = next_midpoint_id--;
            m.demand = a.demand + b.demand;
            m.members = a.members;
            m.members.insert(m.members.end(), b.members.begin(), b.members.end());
            m.coordinates.x = (a.coordinates.x + b.coordinates.x) / 2.0;
            m.coordinates.y = (a.coordinates.y + b.coordinates.y) / 2.0;

            pair_nodes.push_back(std::move(m));
        }

        if (pair_nodes.size() < 2) {
            // No second matching possible: finalize all 2-clusters.
            for (const auto& p : pair_nodes) {
                final_clusters.push_back(p.members);
            }

            std::vector<int> next_lone(next_lone_ids.begin(), next_lone_ids.end());
            std::sort(next_lone.begin(), next_lone.end());

            if (next_lone == lone_ids) {
                break; // no progress possible
            }

            lone_ids = std::move(next_lone);
            continue;
        }

        // Step 3: run matching on midpoint graph.
        auto g2 = build_complete_graph_from_clusters(pair_nodes);
        if (g2.edges.empty()) {
            break;
        }

        graph::Graph G2(g2);
        auto second_matching = solver.get_minimal_weighted_matching(G2);

        std::unordered_map<int, ClusterNode> midpoint_by_id;
        midpoint_by_id.reserve(pair_nodes.size());
        for (const auto& m : pair_nodes) {
            midpoint_by_id[m.id] = m;
        }

        std::unordered_set<int> used_midpoints;
        used_midpoints.reserve(pair_nodes.size());

        // Step 4: for each matched midpoint pair, try merge 4 then merge 3.
        for (const auto& [x_id, y_id] : second_matching.matched_edges) {
            used_midpoints.insert(x_id);
            used_midpoints.insert(y_id);

            const auto& x = midpoint_by_id.at(x_id);
            const auto& y = midpoint_by_id.at(y_id);

            const int a_id = x.members[0];
            const int b_id = x.members[1];
            const int c_id = y.members[0];
            const int d_id = y.members[1];

            const int a_d = lone_by_id.at(a_id).demand;
            const int b_d = lone_by_id.at(b_id).demand;
            const int c_d = lone_by_id.at(c_id).demand;
            const int d_d = lone_by_id.at(d_id).demand;

            const int demand4 = a_d + b_d + c_d + d_d;
            if (demand4 <= capacity) {
                final_clusters.push_back({a_id, b_id, c_id, d_id});
                continue;
            }

            struct TripleCandidate {
                bool feasible;
                int demand;
                int leftover;
            };

            std::vector<TripleCandidate> candidates = {
                {a_d + b_d + c_d <= capacity, a_d + b_d + c_d, d_id},
                {a_d + b_d + d_d <= capacity, a_d + b_d + d_d, c_id},
                {c_d + d_d + a_d <= capacity, c_d + d_d + a_d, b_id},
                {c_d + d_d + b_d <= capacity, c_d + d_d + b_d, a_id}
            };

            auto best = std::max_element(
                candidates.begin(),
                candidates.end(),
                [](const TripleCandidate& lhs, const TripleCandidate& rhs) {
                    return (lhs.feasible ? lhs.demand : -1) < (rhs.feasible ? rhs.demand : -1);
                }
            );

            if (best != candidates.end() && best->feasible) {
                if (best->leftover == d_id) final_clusters.push_back({a_id, b_id, c_id});
                else if (best->leftover == c_id) final_clusters.push_back({a_id, b_id, d_id});
                else if (best->leftover == b_id) final_clusters.push_back({c_id, d_id, a_id});
                else final_clusters.push_back({c_id, d_id, b_id});

                next_lone_ids.insert(best->leftover);
            } else {
                // Could not merge to 3/4: keep two 2-clusters.
                final_clusters.push_back({a_id, b_id});
                final_clusters.push_back({c_id, d_id});
            }
        }

        // Midpoints unmatched at second level => keep as 2-clusters.
        for (const auto& m : pair_nodes) {
            if (used_midpoints.count(m.id)) {
                continue;
            }
            final_clusters.push_back(m.members);
        }

        // Step 5: restart from lone vertices.
        std::vector<int> next_lone(next_lone_ids.begin(), next_lone_ids.end());
        std::sort(next_lone.begin(), next_lone.end());

        if (next_lone == lone_ids) {
            break; // cannot reduce further
        }

        lone_ids = std::move(next_lone);
    }

    // Remaining lone vertices are final singleton clusters.
    for (int id : lone_ids) {
        final_clusters.push_back({id});
    }

    return final_clusters;
}

} // namespace mcvrp::cluster_first
