#include "parser/cvrp_parser.hpp"
#include "solver/ca_mis_solver.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop(false);

void handle_sigint(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

struct ScenarioDefaults {
    int max_subset = 3;
    int capacity = 100;
};

ScenarioDefaults scenario_defaults(const std::string& scenario, const mcvrp::ca_mis::Dataset& dataset) {
    int max_w = 0;
    for (int w : dataset.weights) max_w = std::max(max_w, w);
    if (scenario == "scenario1") {
        return {2, 2 * max_w};
    }
    if (scenario == "scenario2") {
        return {4, 2 * max_w};
    }
    if (scenario == "scenario3") {
        return {2, (3 * max_w) / 2};
    }
    return {3, 100};
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_sigint);

    std::string input = "../datasets.txt";
    std::string datasets_raw = "1-100";
    std::string scenario;
    bool debug = false;
    int max_subset_override = -1;
    int capacity_override = -1;
    int workers = 0;
    int neighbor_limit = 12;
    int max_candidates = 250000;
    int restarts = 8;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--input") input = next(arg);
        else if (arg == "--datasets") datasets_raw = next(arg);
        else if (arg == "--scenario") scenario = next(arg);
        else if (arg == "--max-subset") max_subset_override = std::stoi(next(arg));
        else if (arg == "--capacity") capacity_override = std::stoi(next(arg));
        else if (arg == "--workers") workers = std::stoi(next(arg));
        else if (arg == "--neighbor-limit") neighbor_limit = std::stoi(next(arg));
        else if (arg == "--max-candidates") max_candidates = std::stoi(next(arg));
        else if (arg == "--restarts") restarts = std::stoi(next(arg));
        else if (arg == "--debug") debug = true;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ca_mis_main [--input PATH] [--datasets 1-100] \\n"
                         "  --scenario scenario1|scenario2|scenario3 \\n"
                         "  [--max-subset N] [--capacity N] [--workers N] \\n"
                         "  [--neighbor-limit N] [--max-candidates N] [--restarts N] [--debug]\n";
            return 0;
        }
    }

    if (scenario != "scenario1" && scenario != "scenario2" && scenario != "scenario3") {
        std::cerr << "Missing or invalid --scenario (use scenario1|scenario2|scenario3).\n";
        return 1;
    }

    auto datasets = mcvrp::ca_mis::parser::parse_cvrp_data(input);
    std::vector<int> ids = mcvrp::ca_mis::utils::parse_dataset_selection(datasets_raw);
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int id) { return !datasets.count(id); }), ids.end());
    if (ids.empty()) {
        std::cerr << "No matching datasets found for selection: " << datasets_raw << "\n";
        return 1;
    }

    const unsigned hw = std::thread::hardware_concurrency();
    int ncpus = (workers > 0) ? workers : std::min(15, hw ? static_cast<int>(hw) : 1);
    if ((max_subset_override >= 4 || scenario == "scenario2") && workers <= 0) {
        ncpus = std::min(ncpus, 4);
    }

    if (debug) {
        std::cout << "Loaded " << datasets.size() << " datasets | CPUs: " << ncpus << "\n\n";
    }

    std::vector<mcvrp::ca_mis::DatasetResult> results(ids.size());
    const auto wall_start = std::chrono::high_resolution_clock::now();

    mcvrp::ca_mis::utils::parallel_for(0, static_cast<int>(ids.size()), ncpus, [&](int i) {
        if (g_stop.load(std::memory_order_relaxed)) {
            return;
        }
        const auto dataset_start = std::chrono::high_resolution_clock::now();
        const int id = ids[i];
        auto defaults = scenario_defaults(scenario, datasets[id]);
        const int max_subset = (max_subset_override > 0) ? max_subset_override : defaults.max_subset;
        const int capacity = (capacity_override > 0) ? capacity_override : defaults.capacity;
        results[i] = mcvrp::ca_mis::solver::process_dataset(
            id,
            datasets[id],
            max_subset,
            capacity,
            neighbor_limit,
            max_candidates,
            restarts
        );
        results[i].time_sec = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - dataset_start).count();
    });

    if (g_stop.load(std::memory_order_relaxed)) {
        std::cerr << "Interrupted. Exiting early.\n";
        return 130;
    }

    const double wall = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - wall_start).count();

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });

    if (debug) {
        std::cout << std::left << std::setw(12) << "Dataset"
                  << std::setw(12) << "Status"
                  << std::right << std::setw(14) << "Cost"
                  << std::setw(10) << "Time(s)" << "\n"
                  << std::string(52, '-') << "\n";
    }

    int ok = 0;
    double cost_sum = 0.0;
    for (const auto& r : results) {
        std::ostringstream cost_stream;
        if (r.cost > 0.0) {
            cost_stream << std::fixed << std::setprecision(2) << r.cost;
        } else {
            cost_stream << "N/A";
        }

        if (debug) {
            std::cout << std::left << std::setw(12) << r.id
                      << std::setw(12) << r.status
                      << std::right << std::setw(14) << cost_stream.str()
                      << std::setw(9) << std::fixed << std::setprecision(3) << r.time_sec << "s\n";
        }

        if (r.status == "Optimal") {
            ++ok;
            cost_sum += r.cost;
        }
    }

    if (debug) {
        std::cout << "\nTotal wall-clock time : " << std::fixed << std::setprecision(2) << wall << "s\n";
        std::cout << "Datasets solved       : " << ok << " / " << results.size() << "\n";
        if (ok) {
            std::cout << "Average cost          : " << std::fixed << std::setprecision(2) << cost_sum / ok << "\n";
        }
    }

    mcvrp::ca_mis::utils::write_results("ca_mis", scenario, results, datasets);

    return 0;
}
