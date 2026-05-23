#include "../parser/json_parser.hpp"
#include "../parser/datasets_parser.hpp"
#include "../graph/graph.hpp"
#include "../solver/solver.hpp"
#include "../utils/utils.hpp"
#include "../utils/types.hpp"
#include <unordered_set>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {
int calculate_capacity(const mcvrp::types::GraphInput& input) {
    int max_demand = 0;
    for (const auto& node : input.nodes) {
        if (node.id != 0) {
            max_demand = std::max(max_demand, node.demand);
        }
    }
    return 2 * max_demand; 
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
            std::cerr << "Usage: scenario1 [datasets.txt] [--debug]\n";
            return 1;
        }
    }

    if (test_file.empty()) {
        std::cerr << "Usage: scenario1 [datasets.txt] [--debug]\n";
        return 1;
    }
    if (!fs::exists(test_file) || !fs::is_regular_file(test_file) || test_file.size() < 4 || test_file.substr(test_file.size() - 4) != ".txt") {
        std::cerr << "Input must be a .txt datasets file: " << test_file << "\n";
        return 1;
    }

    const auto datasets = parser::parse_datasets_txt_raw(test_file);

    const fs::path output_root = "../output/cluster_first";
    const fs::path scenario_dir = output_root / "scenario1";
    fs::create_directories(scenario_dir);

    std::ofstream combined_out = utils::open_combined_csv(scenario_dir, "sc_1_combined.csv");

    const fs::path final_csv = output_root / "final_output.csv";
    utils::reset_csv(final_csv);

    std::vector<types::TestResult> results;

    for (const auto& dataset : datasets) {
        std::ostringstream ds_name;
        ds_name << "dataset_" << std::setw(3) << std::setfill('0') << dataset.index;
        const std::string dataset_name = ds_name.str();

        const auto clusters = parser::build_cluster_graphs(dataset, false);

        std::ofstream ds_out = utils::open_dataset_csv(
            scenario_dir,
            "sc_1_ds_" + std::to_string(dataset.index) + ".csv"
        );

        long long dataset_total = 0;
        double dataset_time = 0.0;

        for (const auto& cluster : clusters) {
            const std::string& cluster_name = cluster.first;
            auto input = cluster.second;
            input.capacity = calculate_capacity(input);
            graph::Graph G(input);

            auto start = std::chrono::high_resolution_clock::now();
            solver::Solver solver;
            auto matching = solver.get_minimal_weighted_matching(G);
            auto end = std::chrono::high_resolution_clock::now();

            // Scenario1 total:
            // matching + 2 * sum distance(source, v), source excluded from matching
            const auto& ids = G.get_index_to_id();
            const auto& id_to_index = G.get_id_to_index();

            const int depot_id = (id_to_index.find(0) != id_to_index.end()) ? 0 : ids.front();

            long long depot_roundtrip = static_cast<long long>(matching.total_weight);
            for (int id : ids) {
                if (id == depot_id) continue;
                depot_roundtrip += 2LL * G.distance_by_id(depot_id, id);
            }

            long long total = static_cast<long long>(matching.total_weight); 
            if (total > std::numeric_limits<int>::max()) {
                throw std::overflow_error("Scenario1 total exceeds int range.");
            }

            std::vector<std::vector<int>> tours;
            std::unordered_set<int> used;

            for (const auto& [u, v] : matching.matched_edges) {
                tours.push_back({0, u, v});
                used.insert(u);
                used.insert(v);
                total += static_cast<long long>(G.distance_by_id(depot_id, u));
                total += static_cast<long long>(G.distance_by_id(depot_id, v));
            }

            // add lone non-source vertices
            for (int id : G.get_index_to_id()) {
                if (id == 0) continue;
                if (!used.count(id)){
                    tours.push_back({0, id});
                    total += 2 * static_cast<long long>(G.distance_by_id(depot_id, id));
                }
            }

            types::TestResult r;
            r.test_name = dataset_name + "/" + cluster_name;
            r.num_edges = input.edges.size();
            r.total_weight = static_cast<int>(total);
            r.runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
            results.push_back(r);

            if (debug) {
                std::cout << "\n" << r.test_name << " tours:\n";
                utils::print_tours(tours);
            }

            utils::write_result_row(
                ds_out,
                "scenario1",
                cluster_name,
                static_cast<int>(total),
                r.runtime_ms,
                tours
            );

            utils::append_result_csv(
                final_csv.string(),
                "scenario1",
                r.test_name,
                static_cast<int>(total),
                r.runtime_ms,
                tours
            );

            dataset_total += static_cast<long long>(total);
            dataset_time += r.runtime_ms;
        }

        utils::write_combined_row(combined_out, "scenario1", dataset_name, dataset_total, dataset_time);
    }

    if (debug) {
        utils::print_results(results);
    }
    return 0;
}
