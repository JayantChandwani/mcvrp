#include "match_first_helpers.hpp"

#include "../graph/graph.hpp"
#include "../solver/solver.hpp"
#include "utils.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace mcvrp::match_first {
namespace {

using parser::Dataset;
using types::Coordinates;
using types::GraphInput;
using types::MatchingResult;
using types::Node;

struct ClusterNode {
    int id = 0;
    int demand = 0;
    Group members;
    Coordinates coordinates{};
};

int max_demand(const Dataset& dataset) {
    int max_weight = 0;
    for (int demand : dataset.weights) {
        max_weight = std::max(max_weight, demand);
    }
    return max_weight;
}

std::size_t target_index_of(int target_id, const Dataset& dataset) {
    return static_cast<std::size_t>(target_id - static_cast<int>(dataset.depots.size()));
}

int depot_distance(const Dataset& dataset, int depot_id, int target_id) {
    return utils::euclidean_distance_2dp_scaled(
        dataset.depots[static_cast<std::size_t>(depot_id)],
        dataset.targets[target_index_of(target_id, dataset)]
    );
}

int target_distance(const Dataset& dataset, int lhs, int rhs) {
    return utils::euclidean_distance_2dp_scaled(
        dataset.targets[target_index_of(lhs, dataset)],
        dataset.targets[target_index_of(rhs, dataset)]
    );
}

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

std::unordered_set<int> get_unmatched_ids(
    const std::vector<ClusterNode>& nodes,
    const MatchingResult& matching
) {
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

} // namespace

GraphInput build_target_graph(const Dataset& dataset) {
    GraphInput g;
    g.nodes.reserve(dataset.targets.size());
    const int target_base = static_cast<int>(dataset.depots.size());

    for (std::size_t i = 0; i < dataset.targets.size(); ++i) {
        g.nodes.push_back({
            target_base + static_cast<int>(i),
            dataset.weights[i],
            dataset.targets[i]
        });
    }

    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        for (std::size_t j = i + 1; j < g.nodes.size(); ++j) {
            const int w = utils::euclidean_distance_2dp_scaled(
                g.nodes[i].coordinates,
                g.nodes[j].coordinates
            );
            g.edges.push_back({g.nodes[i].id, g.nodes[j].id, w});
        }
    }

    return g;
}

int scenario2_capacity(const Dataset& dataset) {
    return 2 * max_demand(dataset);
}

int scenario3_capacity(const Dataset& dataset) {
    return 3 * max_demand(dataset) / 2;
}

Coordinates centroid_of(const Group& group, const Dataset& dataset) {
    Coordinates c{};
    if (group.empty()) {
        return c;
    }

    for (int id : group) {
        const auto& p = dataset.targets[target_index_of(id, dataset)];
        c.x += p.x;
        c.y += p.y;
    }
    c.x /= static_cast<double>(group.size());
    c.y /= static_cast<double>(group.size());
    return c;
}

int nearest_depot_id(const Coordinates& point, const Dataset& dataset) {
    int best_id = 0;
    double best_dist = std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < dataset.depots.size(); ++i) {
        const double dx = point.x - dataset.depots[i].x;
        const double dy = point.y - dataset.depots[i].y;
        const double dist = dx * dx + dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best_id = static_cast<int>(i);
        }
    }

    return best_id;
}

std::vector<Group> build_matched_groups(const Dataset& dataset, const MatchingResult& matching) {
    std::vector<Group> groups;
    groups.reserve(matching.matched_edges.size() + dataset.targets.size() % 2);

    std::unordered_set<int> used;
    used.reserve(dataset.targets.size());

    for (const auto& [u, v] : matching.matched_edges) {
        groups.push_back({u, v});
        used.insert(u);
        used.insert(v);
    }

    for (std::size_t i = 0; i < dataset.targets.size(); ++i) {
        const int id = static_cast<int>(dataset.depots.size() + i);
        if (!used.count(id)) {
            groups.push_back({id});
        }
    }

    return groups;
}

long long route_distance(const Group& group, int depot_id, const Dataset& dataset) {
    if (group.empty()) {
        return 0;
    }
    if (group.size() == 1) {
        return 2LL * depot_distance(dataset, depot_id, group.front());
    }

    return static_cast<long long>(depot_distance(dataset, depot_id, group[0])) +
        target_distance(dataset, group[0], group[1]) +
        depot_distance(dataset, depot_id, group[1]);
}

long long cluster_distance(const Group& ordered_cluster, const Dataset& dataset, int depot_id) {
    if (ordered_cluster.empty()) {
        return 0;
    }

    long long total = depot_distance(dataset, depot_id, ordered_cluster.front());
    for (std::size_t i = 1; i < ordered_cluster.size(); ++i) {
        total += target_distance(dataset, ordered_cluster[i - 1], ordered_cluster[i]);
    }
    total += depot_distance(dataset, depot_id, ordered_cluster.back());

    return total;
}

Group build_greedy_cluster_order(const Group& cluster, const Dataset& dataset, int depot_id) {
    if (cluster.size() <= 1) {
        return cluster;
    }

    Group unvisited = cluster;
    Group ordered;
    ordered.reserve(cluster.size());

    auto cmp_from_depot = [&](int lhs, int rhs) {
        const int d_lhs = depot_distance(dataset, depot_id, lhs);
        const int d_rhs = depot_distance(dataset, depot_id, rhs);
        return d_lhs == d_rhs ? lhs < rhs : d_lhs < d_rhs;
    };

    auto start_it = std::min_element(unvisited.begin(), unvisited.end(), cmp_from_depot);
    const int current = *start_it;
    ordered.push_back(current);
    unvisited.erase(start_it);
    std::sort(unvisited.begin(), unvisited.end());

    long long best_cost = std::numeric_limits<long long>::max();
    Group best_tail = unvisited;

    do {
        long long cost = depot_distance(dataset, depot_id, current);
        int prev = current;
        for (int node_id : unvisited) {
            cost += target_distance(dataset, prev, node_id);
            prev = node_id;
        }
        cost += depot_distance(dataset, depot_id, prev);

        if (cost < best_cost || (cost == best_cost && unvisited < best_tail)) {
            best_cost = cost;
            best_tail = unvisited;
        }
    } while (std::next_permutation(unvisited.begin(), unvisited.end()));

    ordered.insert(ordered.end(), best_tail.begin(), best_tail.end());
    return ordered;
}

std::vector<Group> build_scenario2_groups(const Dataset& dataset, const GraphInput& input, int capacity_override) {
    const int capacity = (capacity_override > 0) ? capacity_override : scenario2_capacity(dataset);

    std::unordered_map<int, Node> original_by_id;
    original_by_id.reserve(input.nodes.size());
    for (const auto& n : input.nodes) {
        original_by_id[n.id] = n;
    }

    std::vector<int> lone_ids;
    lone_ids.reserve(input.nodes.size());
    for (const auto& n : input.nodes) {
        lone_ids.push_back(n.id);
    }

    std::vector<Group> final_clusters;

    while (lone_ids.size() >= 2) {
        const auto lone_vertices = build_lone_nodes(lone_ids, original_by_id);

        std::unordered_map<int, ClusterNode> lone_by_id;
        lone_by_id.reserve(lone_vertices.size());
        for (const auto& lv : lone_vertices) {
            lone_by_id[lv.id] = lv;
        }

        const auto g1 = build_complete_graph_from_clusters(lone_vertices);
        if (g1.edges.empty()) {
            break;
        }

        graph::Graph G1(g1);
        solver::Solver solver;
        const auto first_matching = solver.get_minimal_weighted_matching(G1);
        std::unordered_set<int> next_lone_ids = get_unmatched_ids(lone_vertices, first_matching);

        std::vector<ClusterNode> pair_nodes;
        pair_nodes.reserve(first_matching.matched_edges.size());
        int next_midpoint_id = -1;

        for (const auto& [u, v] : first_matching.matched_edges) {
            const auto& a = lone_by_id.at(u);
            const auto& b = lone_by_id.at(v);
            Group members = a.members;
            members.insert(members.end(), b.members.begin(), b.members.end());

            pair_nodes.push_back({
                next_midpoint_id--,
                a.demand + b.demand,
                std::move(members),
                {(a.coordinates.x + b.coordinates.x) / 2.0, (a.coordinates.y + b.coordinates.y) / 2.0}
            });
        }

        if (pair_nodes.size() < 2) {
            for (const auto& p : pair_nodes) {
                final_clusters.push_back(p.members);
            }

            std::vector<int> next_lone(next_lone_ids.begin(), next_lone_ids.end());
            std::sort(next_lone.begin(), next_lone.end());
            if (next_lone == lone_ids) {
                break;
            }
            lone_ids = std::move(next_lone);
            continue;
        }

        graph::Graph G2(build_complete_graph_from_clusters(pair_nodes));
        const auto second_matching = solver.get_minimal_weighted_matching(G2);

        std::unordered_map<int, ClusterNode> midpoint_by_id;
        midpoint_by_id.reserve(pair_nodes.size());
        for (const auto& m : pair_nodes) {
            midpoint_by_id[m.id] = m;
        }

        std::unordered_set<int> used_midpoints;
        used_midpoints.reserve(pair_nodes.size());

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
                bool feasible = false;
                int demand = 0;
                int leftover = 0;
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
                final_clusters.push_back({a_id, b_id});
                final_clusters.push_back({c_id, d_id});
            }
        }

        for (const auto& m : pair_nodes) {
            if (!used_midpoints.count(m.id)) {
                final_clusters.push_back(m.members);
            }
        }

        std::vector<int> next_lone(next_lone_ids.begin(), next_lone_ids.end());
        std::sort(next_lone.begin(), next_lone.end());
        if (next_lone == lone_ids) {
            break;
        }
        lone_ids = std::move(next_lone);
    }

    for (int id : lone_ids) {
        final_clusters.push_back({id});
    }

    return final_clusters;
}

} // namespace mcvrp::match_first
