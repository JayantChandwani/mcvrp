#include "../parser/json_parser.hpp"
#include "../parser/datasets_parser.hpp"
#include "../graph/graph.hpp"
#include "../solver/solver.hpp"
#include "../utils/types.hpp"
#include "../utils/utils.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using mcvrp::types::GraphInput;
using mcvrp::types::MatchingResult;
using mcvrp::types::Node;

struct ClusterNode {
    int id = 0;
    int demand = 0;
    std::vector<int> members;
    mcvrp::types::Coordinates coordinates{};
};

int calculate_capacity(const GraphInput& input) {
    int max_demand = 0;
    for (const auto& n : input.nodes) {
        if (n.id != 0) {
            max_demand = std::max(max_demand, n.demand);
        }
    }
    return 2 * max_demand; // wi <= C/2
}

GraphInput build_complete_graph_from_clusters(const std::vector<ClusterNode>& clusters) {
    GraphInput g;
    g.nodes.reserve(clusters.size());

    for (const auto& c : clusters) {
        Node n;
        n.id = c.id;
        n.demand = c.demand;
        n.coordinates = c.coordinates;
        g.nodes.push_back(n);
    }

    for (std::size_t i = 0; i < clusters.size(); ++i) {
        for (std::size_t j = i + 1; j < clusters.size(); ++j) {
            const int w = mcvrp::utils::euclidean_distance_2dp_scaled(
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
    const std::unordered_map<int, mcvrp::types::Node>& original_by_id
) {
    std::vector<ClusterNode> lone_nodes;
    lone_nodes.reserve(lone_ids.size());

    for (int id : lone_ids) {
        const auto& n = original_by_id.at(id);
        ClusterNode c;
        c.id = id;
        c.demand = n.demand;
        c.coordinates = n.coordinates;
        c.members = {id};
        lone_nodes.push_back(std::move(c));
    }

    return lone_nodes;
}

long long cluster_distance(
    const std::vector<int>& ordered_cluster,
    const mcvrp::graph::Graph& base_graph,
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

std::vector<int> build_greedy_cluster_order(
    const std::vector<int>& cluster,
    const mcvrp::graph::Graph& base_graph,
    int depot_id
) {
    if (cluster.empty()) return {};
    if (cluster.size() == 1) return cluster;

    // In-cluster route construction only:
    // 1) start from the vertex closest to source,
    // 2) choose the best ordering of remaining vertices,
    // 3) return the resulting visit sequence.
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

struct OutputRow {
    std::string test_name;
    std::string cluster_name;
    int total = 0;
    double runtime_ms = 0.0;
    std::vector<std::vector<int>> tours;
};

} // namespace

int main(int argc, char** argv) {
    using namespace mcvrp;

    bool debug = false;
    std::string test_file;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--debug") {
            debug = true;
        } else if (test_file.empty()) {
            test_file = arg;
        } else {
            std::cerr << "Usage: scenario2 [datasets.txt] [--debug]\n";
            return 1;
        }
    }

    if (test_file.empty()) {
        std::cerr << "Usage: scenario2 [datasets.txt] [--debug]\n";
        return 1;
    }

    if (debug) {
        std::cout << "=== Scenario 2: wi <= C/2 and no limits on K ===\n\n";
    }
    if (!std::filesystem::exists(test_file) || !std::filesystem::is_regular_file(test_file) || test_file.size() < 4 || test_file.substr(test_file.size() - 4) != ".txt") {
        std::cerr << "Input must be a .txt datasets file: " << test_file << "\n";
        return 1;
    }

    const auto datasets = parser::parse_datasets_txt_raw(test_file);

    const std::filesystem::path output_root = "../output/cluster_first";
    const std::filesystem::path scenario_dir = output_root / "scenario2";
    std::filesystem::create_directories(scenario_dir);

    std::ofstream combined_out = utils::open_combined_csv(scenario_dir, "sc_2_combined.csv");

    const std::filesystem::path final_csv = output_root / "final_output.csv";
    utils::reset_csv(final_csv);

    std::vector<types::TestResult> results;

    for (const auto& dataset : datasets) {
        std::ostringstream ds_name;
        ds_name << "dataset_" << std::setw(3) << std::setfill('0') << dataset.index;
        const std::string dataset_name = ds_name.str();

        std::ofstream ds_out = utils::open_dataset_csv(
            scenario_dir,
            "sc_2_ds_" + std::to_string(dataset.index) + ".csv"
        );

        auto dataset_start = std::chrono::high_resolution_clock::now();
        const auto clusters = parser::build_cluster_graphs(dataset, false);

        long long dataset_total = 0;
        std::vector<OutputRow> output_rows;

        for (const auto& cluster : clusters) {
            const std::string& cluster_name = cluster.first;
            auto input = cluster.second;

        const int capacity = (input.capacity > 0) ? input.capacity : calculate_capacity(input);

        std::unordered_map<int, types::Node> original_by_id;
        original_by_id.reserve(input.nodes.size());
        for (const auto& n : input.nodes) {
            original_by_id[n.id] = n;
        }

        graph::Graph base_graph(input);

        std::vector<int> lone_ids;
        lone_ids.reserve(input.nodes.size());
        for (const auto& n : input.nodes) {
            if (n.id == 0) continue; // exclude source from all matchings
            lone_ids.push_back(n.id);
        }

        std::vector<std::vector<int>> final_clusters;
        std::size_t rounds = 0;

        auto start = std::chrono::high_resolution_clock::now();

        while (lone_ids.size() >= 2) {
            ++rounds;

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

        auto end = std::chrono::high_resolution_clock::now();

        // Final total from final clusters:
        //  - size 1: 2 * source distance
        //  - size >= 2: path inside each fixed cluster only
        //    (source -> closest-to-source -> best remaining order -> source)
        const auto& id_to_index = base_graph.get_id_to_index();
        const auto& index_to_id = base_graph.get_index_to_id();
        const int depot_id = (id_to_index.find(0) != id_to_index.end()) ? 0 : index_to_id.front();

        long long final_total = 0;
        // std::cout << "Clusters for " << path.filename().string() << ":\n";
        for (const auto& cluster : final_clusters) {
            const auto ordered = build_greedy_cluster_order(cluster, base_graph, depot_id);
            final_total += cluster_distance(ordered, base_graph, depot_id);
            // auto printable = cluster;
            // std::sort(printable.begin(), printable.end());

            // std::cout << "  {";
            // for (std::size_t i = 0; i < printable.size(); ++i) {
            //     std::cout << printable[i];
            //     if (i + 1 < printable.size()) std::cout << ", ";
            // }
            // std::cout << "}\n";
        }
        if (final_total > std::numeric_limits<int>::max()) {
            throw std::overflow_error("Scenario2 total exceeds int range.");
        }

        types::TestResult tr;
        tr.test_name = dataset_name + "/" + cluster_name;
        tr.total_weight = static_cast<int>(final_total);
        tr.num_edges = input.edges.size();
        tr.runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
        results.push_back(tr);

        std::vector<std::vector<int>> tours;
        tours.reserve(final_clusters.size());
        for (const auto& cluster : final_clusters) {
            std::vector<int> t;
            t.push_back(0);
            const auto ordered = build_greedy_cluster_order(cluster, base_graph, depot_id);
            t.insert(t.end(), ordered.begin(), ordered.end());
            tours.push_back(std::move(t));
        }

        if (debug) {
            std::cout << dataset_name << "/" << cluster_name << " tours:\n";
            utils::print_tours(tours);
        }

        output_rows.push_back({
            dataset_name + "/" + cluster_name,
            cluster_name,
            static_cast<int>(final_total),
            tr.runtime_ms,
            tours
        });

        dataset_total += final_total;
        }

        const auto dataset_end = std::chrono::high_resolution_clock::now();
        const double dataset_time = std::chrono::duration<double, std::milli>(dataset_end - dataset_start).count();

        for (const auto& row : output_rows) {
        utils::write_result_row(
            ds_out,
            "scenario2",
            row.cluster_name,
            row.total,
            row.runtime_ms,
            row.tours
        );

        utils::append_result_csv(
            final_csv.string(),
            "scenario2",
            row.test_name,
            row.total,
            row.runtime_ms,
            row.tours
        );
        }

        utils::write_combined_row(combined_out, "scenario2", dataset_name, dataset_total, dataset_time);
    }

    if (debug) {
        utils::print_results(results);
    }
    return 0;
}
