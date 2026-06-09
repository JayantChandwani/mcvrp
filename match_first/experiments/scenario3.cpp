#include "../graph/graph.hpp"
#include "../parser/datasets_parser.hpp"
#include "../solver/solver.hpp"
#include "../utils/edge_filters.hpp"
#include "../utils/match_first_helpers.hpp"
#include "../utils/types.hpp"
#include "../utils/utils.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
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
            std::cerr << "Usage: scenario3 [datasets.txt] [--capacity N] [--debug]\n";
            return 1;
        }
    }

    if (test_file.empty()) {
        std::cerr << "Usage: scenario3 [datasets.txt] [--capacity N] [--debug]\n";
        return 1;
    }
    if (!fs::exists(test_file) || !fs::is_regular_file(test_file) || test_file.size() < 4 || test_file.substr(test_file.size() - 4) != ".txt") {
        std::cerr << "Input must be a .txt datasets file: " << test_file << "\n";
        return 1;
    }

    if (debug) {
        std::cout << "=== Match First Scenario 3: wi <= C/2 and K = 2 ===\n\n";
    }

    const auto datasets = parser::parse_datasets_txt_raw(test_file);

    const fs::path output_root = "../output/match_first";
    const fs::path scenario_dir = output_root / "scenario3";
    fs::create_directories(scenario_dir);

    std::ofstream combined_out = utils::open_combined_csv(scenario_dir, "sc_3_combined.csv");

    const fs::path final_csv = output_root / "final_output.csv";
    utils::reset_csv(final_csv);

    std::vector<types::TestResult> results;

    for (const auto& dataset : datasets) {
        std::ostringstream ds_name;
        ds_name << "dataset_" << std::setw(3) << std::setfill('0') << dataset.index;
        const std::string dataset_name = ds_name.str();

        std::ofstream ds_out = utils::open_dataset_csv(
            scenario_dir,
            "sc_3_ds_" + std::to_string(dataset.index) + ".csv"
        );

        auto start = std::chrono::high_resolution_clock::now();

        auto graph_input = match_first::build_target_graph(dataset);
        graph_input.capacity = (capacity_override > 0) ? capacity_override : match_first::scenario3_capacity(dataset);
        const auto filtered = filters::apply_weight_constraint(graph_input);

        if (debug) {
            std::cout << "Testing: " << dataset_name
                << " (C = " << graph_input.capacity
                << ", edges: " << graph_input.edges.size()
                << " -> " << filtered.edges.size() << ")\n";
        }

        graph::Graph G(filtered);
        solver::Solver solver;
        const auto matching = solver.get_minimal_weighted_matching(G);
        const auto groups = match_first::build_matched_groups(dataset, matching);

        long long total = 0;
        std::vector<std::vector<int>> tours;
        tours.reserve(groups.size());

        for (const auto& group : groups) {
            const int depot_id = match_first::nearest_depot_id(
                match_first::centroid_of(group, dataset),
                dataset
            );
            total += match_first::route_distance(group, depot_id, dataset);

            std::vector<int> tour;
            tour.reserve(group.size() + 1);
            tour.push_back(depot_id);
            tour.insert(tour.end(), group.begin(), group.end());
            tours.push_back(std::move(tour));
        }

        auto end = std::chrono::high_resolution_clock::now();

        if (total > std::numeric_limits<int>::max()) {
            throw std::overflow_error("Scenario3 total exceeds int range.");
        }

        types::TestResult r;
        r.test_name = dataset_name;
        r.num_edges = filtered.edges.size();
        r.total_weight = static_cast<int>(total);
        r.runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
        results.push_back(r);

        if (debug) {
            std::cout << dataset_name << " tours:\n";
            utils::print_tours(tours);
        }

        utils::write_result_row(ds_out, "scenario3", dataset_name, r.total_weight, r.runtime_ms, tours);
        utils::append_result_csv(final_csv.string(), "scenario3", r.test_name, r.total_weight, r.runtime_ms, tours);
        utils::write_combined_row(combined_out, "scenario3", dataset_name, total, r.runtime_ms);
    }

    if (debug) {
        utils::print_results(results);
    }
    return 0;
}
