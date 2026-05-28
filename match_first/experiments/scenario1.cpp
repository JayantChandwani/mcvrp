#include "../graph/graph.hpp"
#include "../parser/datasets_parser.hpp"
#include "../solver/solver.hpp"
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

    const fs::path output_root = "../output/match_first";
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

        std::ofstream ds_out = utils::open_dataset_csv(
            scenario_dir,
            "sc_1_ds_" + std::to_string(dataset.index) + ".csv"
        );

        auto start = std::chrono::high_resolution_clock::now();

        auto input = match_first::build_target_graph(dataset);
        graph::Graph G(input);

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
            throw std::overflow_error("Scenario1 total exceeds int range.");
        }

        types::TestResult r;
        r.test_name = dataset_name;
        r.num_edges = input.edges.size();
        r.total_weight = static_cast<int>(total);
        r.runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
        results.push_back(r);

        if (debug) {
            std::cout << "\n" << r.test_name << " tours:\n";
            utils::print_tours(tours);
        }

        utils::write_result_row(ds_out, "scenario1", dataset_name, r.total_weight, r.runtime_ms, tours);
        utils::append_result_csv(final_csv.string(), "scenario1", r.test_name, r.total_weight, r.runtime_ms, tours);
        utils::write_combined_row(combined_out, "scenario1", dataset_name, total, r.runtime_ms);
    }

    if (debug) {
        utils::print_results(results);
    }
    return 0;
}
