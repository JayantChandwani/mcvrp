#include "parser/cvrp_parser.hpp"
#include "solver/ca_mis_solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop(false);

void handle_sigint(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

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

std::string csv_escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string tour_to_string(const std::vector<int>& tour) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < tour.size(); ++i) {
        os << tour[i];
        if (i + 1 < tour.size()) os << ", ";
    }
    os << "]";
    return os.str();
}

std::string tours_to_string(const std::vector<std::vector<int>>& tours) {
    std::ostringstream os;
    for (std::size_t i = 0; i < tours.size(); ++i) {
        os << tour_to_string(tours[i]);
        if (i + 1 < tours.size()) os << " | ";
    }
    return os.str();
}

std::ofstream open_csv_trunc(const std::filesystem::path& path, const std::string& header) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::trunc);
    if (out.is_open() && !header.empty()) {
        out << header << "\n";
    }
    return out;
}

void reset_csv(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

void append_result_csv(
    const std::filesystem::path& path,
    const std::string& scenario,
    const std::string& test_name,
    double total_distance,
    double runtime_ms,
    const std::vector<std::vector<int>>& tours
) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    const bool write_header = !std::filesystem::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) return;

    if (write_header) {
        out << "scenario,test_name,total_distance,runtime_ms,tours\n";
    }

    out << csv_escape(scenario) << ","
        << csv_escape(test_name) << ","
        << std::fixed << std::setprecision(6) << total_distance << ","
        << std::fixed << std::setprecision(6) << runtime_ms << ","
        << csv_escape(tours_to_string(tours)) << "\n";
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
    std::vector<int> ids = parse_dataset_selection(datasets_raw);
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int id) { return !datasets.count(id); }), ids.end());
    if (ids.empty()) {
        std::cerr << "No matching datasets found for selection: " << datasets_raw << "\n";
        return 1;
    }

    int ncpus = (workers > 0) ? workers : std::min(15, omp_get_max_threads());
    if ((max_subset_override >= 4 || scenario == "scenario2") && workers <= 0) {
        ncpus = std::min(ncpus, 4);
    }
    omp_set_num_threads(ncpus);

    if (debug) {
        std::cout << "Loaded " << datasets.size() << " datasets | CPUs: " << ncpus << "\n\n";
    }

    std::vector<mcvrp::ca_mis::DatasetResult> results(ids.size());
    const auto wall_start = std::chrono::high_resolution_clock::now();

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        if (g_stop.load(std::memory_order_relaxed)) {
            continue;
        }
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
    }

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

    const std::filesystem::path output_root = "../output/ca_mis";
    const std::filesystem::path scenario_dir = output_root / scenario;
    std::filesystem::create_directories(scenario_dir);

    std::string scenario_num = "0";
    if (scenario == "scenario1") scenario_num = "1";
    else if (scenario == "scenario2") scenario_num = "2";
    else if (scenario == "scenario3") scenario_num = "3";

    std::ofstream combined_out = open_csv_trunc(
        scenario_dir / ("sc_" + scenario_num + "_combined.csv"),
        "scenario,dataset,total_distance_sum,total_time_ms_sum"
    );

    const std::filesystem::path final_csv = output_root / "final_output.csv";
    reset_csv(final_csv);

    for (const auto& r : results) {
        std::ostringstream ds_name;
        ds_name << "dataset_" << std::setw(3) << std::setfill('0') << r.id;
        const std::string dataset_name = ds_name.str();

        std::ofstream ds_out = open_csv_trunc(
            scenario_dir / ("sc_" + scenario_num + "_ds_" + std::to_string(r.id) + ".csv"),
            "scenario,test_name,total_distance,runtime_ms,tours"
        );

        const double dataset_time_ms = r.time_sec * 1000.0;
        const std::size_t route_count = r.selected.size();
        const double route_time_ms = route_count ? (dataset_time_ms / route_count) : 0.0;

        double dataset_total = 0.0;
        const int depot_count = static_cast<int>(datasets[r.id].vehicle_coords.size());

        for (std::size_t i = 0; i < r.selected.size(); ++i) {
            const auto& bid = r.selected[i];

            std::vector<int> tour;
            tour.reserve(bid.order.size() + 1);
            tour.push_back(bid.depot);
            const auto& order = bid.order.empty() ? bid.subset : bid.order;
            for (int t : order) {
                tour.push_back(depot_count + t);
            }

            std::vector<std::vector<int>> tours = {tour};
            const std::string route_name = "depot_" + std::to_string(bid.depot + 1)
                + "_route_" + std::to_string(i + 1);

            append_result_csv(
                final_csv,
                scenario,
                dataset_name + "/" + route_name,
                bid.bid,
                route_time_ms,
                tours
            );

            ds_out << csv_escape(scenario) << ","
                   << csv_escape(route_name) << ","
                   << std::fixed << std::setprecision(6) << bid.bid << ","
                   << std::fixed << std::setprecision(6) << route_time_ms << ","
                   << csv_escape(tours_to_string(tours)) << "\n";

            dataset_total += bid.bid;
        }

        combined_out << scenario << ","
                     << dataset_name << ","
                     << std::fixed << std::setprecision(6) << dataset_total << ","
                     << std::fixed << std::setprecision(6) << dataset_time_ms << "\n";
    }

    return 0;
}
