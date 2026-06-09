#include "../parser/datasets_parser.hpp"
#include "../graph/graph.hpp"
#include "../solver/solver.hpp"
#include "../utils/cluster_first_helpers.hpp"
#include "../utils/types.hpp"
#include "../utils/utils.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    using namespace mcvrp;

    bool debug = false;
    std::string test_file;
    int capacity_override = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--debug") {
            debug = true;
        } else if (arg == "--capacity") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --capacity\n";
                return 1;
            }
            capacity_override = std::stoi(argv[++i]);
        } else if (test_file.empty()) {
            test_file = arg;
        } else {
            std::cerr << "Usage: scenario2 [datasets.txt] [--capacity N] [--debug]\n";
            return 1;
        }
    }

    if (test_file.empty()) {
        std::cerr << "Usage: scenario2 [datasets.txt] [--capacity N] [--debug]\n";
        return 1;
    }

    if (debug) {
        std::cout << "=== Scenario 2: wi <= C/2 and no limits on K ===\n\n";
    }
    if (!fs::exists(test_file) || !fs::is_regular_file(test_file) || test_file.size() < 4 || test_file.substr(test_file.size() - 4) != ".txt") {
        std::cerr << "Input must be a .txt datasets file: " << test_file << "\n";
        return 1;
    }

    const auto datasets = parser::parse_datasets_txt_raw(test_file);

    const fs::path output_root = "../output/cluster_first";
    const fs::path scenario_dir = output_root / "scenario2";
    fs::create_directories(scenario_dir);

    std::ofstream combined_out = utils::open_combined_csv(scenario_dir, "sc_2_combined.csv");

    const fs::path final_csv = output_root / "final_output.csv";
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
        std::vector<cluster_first::OutputRow> output_rows;

        for (const auto& cluster : clusters) {
            const std::string& cluster_name = cluster.first;
            const auto& input = cluster.second;

            const int capacity = (capacity_override > 0)
                ? capacity_override
                : (input.capacity > 0) ? input.capacity : cluster_first::scenario2_capacity(input);

            auto start = std::chrono::high_resolution_clock::now();
            const auto final_clusters = cluster_first::build_scenario2_clusters(input, capacity);
            auto end = std::chrono::high_resolution_clock::now();

            graph::Graph base_graph(input);
            const auto& id_to_index = base_graph.get_id_to_index();
            const auto& index_to_id = base_graph.get_index_to_id();
            const int depot_id = (id_to_index.find(0) != id_to_index.end()) ? 0 : index_to_id.front();

            long long final_total = 0;
            std::vector<std::vector<int>> tours;
            tours.reserve(final_clusters.size());

            for (const auto& fc : final_clusters) {
                const auto ordered = cluster_first::build_greedy_cluster_order(fc, base_graph, depot_id);
                final_total += cluster_first::cluster_distance(ordered, base_graph, depot_id);

                std::vector<int> t;
                t.push_back(0);
                t.insert(t.end(), ordered.begin(), ordered.end());
                tours.push_back(std::move(t));
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
            utils::write_result_row(ds_out, "scenario2", row.cluster_name, row.total, row.runtime_ms, row.tours);
            utils::append_result_csv(final_csv.string(), "scenario2", row.test_name, row.total, row.runtime_ms, row.tours);
        }

        utils::write_combined_row(combined_out, "scenario2", dataset_name, dataset_total, dataset_time);
    }

    if (debug) {
        utils::print_results(results);
    }
    return 0;
}
