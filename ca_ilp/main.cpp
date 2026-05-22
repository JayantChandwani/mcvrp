#include "parser/cvrp_parser.hpp"
#include "solver/ca_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<int> parse_dataset_selection(const std::string& raw) {
    std::vector<int> ids;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        auto dash = token.find('-');
        if (dash != std::string::npos) {
            int a = std::stoi(token.substr(0, dash));
            int b = std::stoi(token.substr(dash + 1));
            if (a > b) std::swap(a, b);
            for (int i = a; i <= b; ++i) ids.push_back(i);
        } else {
            ids.push_back(std::stoi(token));
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

struct ScenarioDefaults {
    int max_subset = 3;
    int capacity = 100;
};

ScenarioDefaults scenario_defaults(const std::string& scenario, const mcvrp::ca::Dataset& dataset) {
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

void write_results_csv(
    const std::vector<mcvrp::ca::DatasetResult>& results,
    const std::string& scenario,
    const std::string& output_csv
) {
    if (output_csv.empty()) return;
    std::ofstream out(output_csv, std::ios::trunc);
    if (!out.is_open()) return;

    out << "scenario,dataset_num,status,total_distance,runtime_s,feasible_subsets\n";
    for (const auto& r : results) {
        out << scenario << ","
            << r.id << ","
            << r.status << ",";
        if (r.cost > 0.0) {
            out << std::fixed << std::setprecision(6) << r.cost;
        }
        out << "," << std::fixed << std::setprecision(6) << r.time_sec
            << "," << r.feasible_subsets << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string input = "../datasets.txt";
    std::string datasets_raw = "1-100";
    std::string scenario = "fixed";
    int max_subset_override = -1;
    int capacity_override = -1;
    int workers = 0;
    int neighbor_limit = 12;
    int max_candidates = 250000;
    std::string output_csv;

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
        else if (arg == "--output-csv") output_csv = next(arg);
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ca_ilp_main [--input PATH] [--datasets 1-100] \\n"
                         "  [--scenario scenario1|scenario2|scenario3|fixed] \\n"
                         "  [--max-subset N] [--capacity N] [--workers N] \\n"
                         "  [--neighbor-limit N] [--max-candidates N] [--output-csv PATH]\n";
            return 0;
        }
    }

    auto datasets = mcvrp::ca::parser::parse_cvrp_data(input);
    std::vector<int> ids = parse_dataset_selection(datasets_raw);
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int id) { return !datasets.count(id); }), ids.end());
    if (ids.empty()) {
        std::cerr << "No matching datasets found for selection: " << datasets_raw << "\n";
        return 1;
    }

    int ncpus = omp_get_max_threads();
    if (workers > 0) ncpus = workers;
    if (max_subset_override >= 4 && workers <= 0) {
        ncpus = std::min(ncpus, 4);
    }
    omp_set_num_threads(ncpus);

    std::cout << "Loaded " << datasets.size() << " datasets | CPUs: " << ncpus << "\n\n";

    std::vector<mcvrp::ca::DatasetResult> results(ids.size());
    const auto wall_start = std::chrono::high_resolution_clock::now();

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        const int id = ids[i];
        auto defaults = scenario_defaults(scenario, datasets[id]);
        const int max_subset = (max_subset_override > 0) ? max_subset_override : defaults.max_subset;
        const int capacity = (capacity_override > 0) ? capacity_override : defaults.capacity;
        results[i] = mcvrp::ca::solver::process_dataset(
            id,
            datasets[id],
            max_subset,
            capacity,
            neighbor_limit,
            max_candidates
        );
    }

    const double wall = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - wall_start).count();

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });

    std::cout << std::left << std::setw(12) << "Dataset"
              << std::setw(12) << "Status"
              << std::right << std::setw(14) << "Cost"
              << std::setw(10) << "Time(s)" << "\n"
              << std::string(52, '-') << "\n";

    int ok = 0;
    double cost_sum = 0.0;
    for (const auto& r : results) {
        std::ostringstream cost_stream;
        if (r.cost > 0.0) {
            cost_stream << std::fixed << std::setprecision(2) << r.cost;
        } else {
            cost_stream << "N/A";
        }

        std::cout << std::left << std::setw(12) << r.id
                  << std::setw(12) << r.status
                  << std::right << std::setw(14) << cost_stream.str()
                  << std::setw(9) << std::fixed << std::setprecision(3) << r.time_sec << "s\n";

        if (r.status == "Optimal") {
            ++ok;
            cost_sum += r.cost;
        }
    }

    std::cout << "\nTotal wall-clock time : " << std::fixed << std::setprecision(2) << wall << "s\n";
    std::cout << "Datasets solved       : " << ok << " / " << results.size() << "\n";
    if (ok) {
        std::cout << "Average cost          : " << std::fixed << std::setprecision(2) << cost_sum / ok << "\n";
    }

    write_results_csv(results, scenario, output_csv);
    return 0;
}