#include "../utils/edge_filters.hpp"
#include "../parser/json_parser.hpp"
#include "../parser/datasets_parser.hpp"
#include "../graph/graph.hpp"
#include "../solver/solver.hpp"
#include "../utils/utils.hpp"
#include "../utils/types.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
int calculate_capacity(const mcvrp::types::GraphInput& input) {
    int max_demand = 0;
    for (const auto& node : input.nodes) {
        if (node.id != 0) {
            max_demand = std::max(max_demand, node.demand);
        }
    }
    return 3 * max_demand / 2;
}
}

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
            std::cerr << "Usage: scenario3 [datasets.txt] [--debug]\n";
            return 1;
        }
    }

    if (test_file.empty()) {
        std::cerr << "Usage: scenario3 [datasets.txt] [--debug]\n";
        return 1;
    }

    if (debug) {
        std::cout << "=== Scenario 3: demand[i] <= C/2 and K = 2 ===\n\n";
    }
    if (!std::filesystem::exists(test_file) || !std::filesystem::is_regular_file(test_file) || test_file.size() < 4 || test_file.substr(test_file.size() - 4) != ".txt") {
        std::cerr << "Input must be a .txt datasets file: " << test_file << "\n";
        return 1;
    }

    const auto datasets = parser::parse_datasets_txt_raw(test_file);

    const std::filesystem::path output_root = "../output/cluster_first";
    const std::filesystem::path scenario_dir = output_root / "scenario3";
    std::filesystem::create_directories(scenario_dir);

    std::ofstream combined_out = utils::open_combined_csv(scenario_dir, "sc_3_combined.csv");

    const std::filesystem::path final_csv = output_root / "final_output.csv";
    utils::reset_csv(final_csv);

    std::vector<types::TestResult> results;

    for (const auto& dataset : datasets) {
        std::ostringstream ds_name;
        ds_name << "dataset_" << std::setw(3) << std::setfill('0') << dataset.index;
        const std::string dataset_name = ds_name.str();

        const auto clusters = parser::build_cluster_graphs(dataset, false);

        std::ofstream ds_out = utils::open_dataset_csv(
            scenario_dir,
            "sc_3_ds_" + std::to_string(dataset.index) + ".csv"
        );

        long long dataset_total = 0;
        double dataset_time = 0.0;

        for (const auto& cluster : clusters) {
            const std::string& cluster_name = cluster.first;
            auto graph_input = cluster.second;
            graph_input.capacity = calculate_capacity(graph_input);

        auto filtered = filters::apply_weight_constraint(graph_input);

        if (debug) {
            std::cout << "Testing: " << dataset_name << "/" << cluster_name
                << " (C = " << graph_input.capacity
                << ", edges: " << graph_input.edges.size()
                << " -> " << filtered.edges.size() << ")\n";
        }

        auto start = std::chrono::high_resolution_clock::now();
        graph::Graph G(filtered);
        solver::Solver solver;
        auto matching = solver.get_minimal_weighted_matching(G);
        auto end = std::chrono::high_resolution_clock::now();

        graph::Graph original_graph(graph_input);
        const auto& ids = original_graph.get_index_to_id();
        const auto& id_to_index = original_graph.get_id_to_index();

        if (ids.empty()) {
            continue;
        }

        const int depot_id = (id_to_index.find(0) != id_to_index.end()) ? 0 : ids.front();

        std::set<int> left;
        for (const auto& n : graph_input.nodes) {
            if (n.id != depot_id) {
                left.insert(n.id);
            }
        }

        long long depot_roundtrip_sum = 0;
        for (const auto& [u, v] : matching.matched_edges) {
            left.erase(u);
            left.erase(v);
            depot_roundtrip_sum += original_graph.distance_by_id(depot_id, u);
            depot_roundtrip_sum += original_graph.distance_by_id(depot_id, v);
        }

        for (const auto& node_id : left) {
            depot_roundtrip_sum += 2LL * original_graph.distance_by_id(depot_id, node_id);
        }

        const long long final_total = static_cast<long long>(matching.total_weight) + depot_roundtrip_sum;
        if (final_total > std::numeric_limits<int>::max()) {
            throw std::overflow_error("Total distance exceeds int range.");
        }

        types::TestResult tr;
        tr.test_name = dataset_name + "/" + cluster_name;
        tr.num_edges = filtered.edges.size();
        tr.total_weight = static_cast<int>(final_total);
        tr.runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
        results.push_back(tr);

        std::vector<std::vector<int>> tours;
        std::unordered_set<int> used;

        for (const auto& [u, v] : matching.matched_edges) {
            tours.push_back({depot_id, u, v});
            used.insert(u);
            used.insert(v);
        }

        for (const auto& n : graph_input.nodes) {
            if (n.id == depot_id) continue;
            if (!used.count(n.id)) {
                tours.push_back({depot_id, n.id});
            }
        }

        if (debug) {
            std::cout << dataset_name << "/" << cluster_name << " tours:\n";
            utils::print_tours(tours);
        }

        utils::write_result_row(
            ds_out,
            "scenario3",
            cluster_name,
            static_cast<int>(final_total),
            tr.runtime_ms,
            tours
        );

        utils::append_result_csv(
            final_csv.string(),
            "scenario3",
            dataset_name + "/" + cluster_name,
            static_cast<int>(final_total),
            tr.runtime_ms,
            tours
        );

        dataset_total += final_total;
        dataset_time += tr.runtime_ms;
        }

        utils::write_combined_row(combined_out, "scenario3", dataset_name, dataset_total, dataset_time);
    }

    if (debug) {
        utils::print_results(results);
    }
    return 0;
}
