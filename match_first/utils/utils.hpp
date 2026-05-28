#pragma once
#include "types.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace mcvrp::utils {

constexpr int DISTANCE_SCALE = 100;

inline int euclidean_distance_2dp_scaled(const types::Coordinates& a, const types::Coordinates& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    return static_cast<int>(std::llround(d * DISTANCE_SCALE));
}

inline double to_display_distance(int scaled) {
    return static_cast<double>(scaled) / DISTANCE_SCALE;
}

inline double to_display_distance(long long scaled) {
    return static_cast<double>(scaled) / DISTANCE_SCALE;
}

inline void print_results(const std::vector<types::TestResult>& results) {
    std::cout << "\n" << std::string(90, '=') << "\n";
    std::cout << "                           TEST RESULTS\n";
    std::cout << std::string(90, '=') << "\n";

    std::cout << std::left << std::setw(25) << "Test Name"
              << std::right << std::setw(18) << "Total Distance"
              << std::setw(12) << "Edges"
              << std::setw(15) << "Time (ms)" << "\n";
    std::cout << std::string(90, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.test_name
                  << std::right << std::setw(18) << std::fixed << std::setprecision(2)
                  << to_display_distance(r.total_weight)
                  << std::setw(12) << r.num_edges
                  << std::setw(15) << std::fixed << std::setprecision(3)
                  << r.runtime_ms << "\n";
    }

    std::cout << std::string(90, '=') << "\n\n";
}

inline std::string tour_to_string(const std::vector<int>& tour) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < tour.size(); ++i) {
        os << tour[i];
        if (i + 1 < tour.size()) os << ", ";
    }
    os << "]";
    return os.str();
}

inline std::string tours_to_string(const std::vector<std::vector<int>>& tours) {
    std::ostringstream os;
    for (std::size_t i = 0; i < tours.size(); ++i) {
        os << tour_to_string(tours[i]);
        if (i + 1 < tours.size()) os << " | ";
    }
    return os.str();
}

inline void print_tours(const std::vector<std::vector<int>>& tours) {
    for (const auto& t : tours) {
        std::cout << tour_to_string(t) << "\n";
    }
}

inline std::string csv_escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

inline std::ofstream open_csv_trunc(const std::filesystem::path& path, const std::string& header) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::trunc);
    if (out.is_open() && !header.empty()) {
        out << header << "\n";
    }
    return out;
}

inline void reset_csv(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

inline std::ofstream open_dataset_csv(const std::filesystem::path& scenario_dir, const std::string& filename) {
    return open_csv_trunc(
        scenario_dir / filename,
        "scenario,test_name,total_distance,runtime_ms,tours"
    );
}

inline std::ofstream open_combined_csv(const std::filesystem::path& scenario_dir, const std::string& filename) {
    return open_csv_trunc(
        scenario_dir / filename,
        "scenario,dataset,total_distance_sum,total_time_ms_sum"
    );
}

inline void write_result_row(
    std::ofstream& out,
    const std::string& scenario,
    const std::string& test_name,
    int total_distance,
    double runtime_ms,
    const std::vector<std::vector<int>>& tours
) {
    if (!out.is_open()) return;
    out << csv_escape(scenario) << ","
        << csv_escape(test_name) << ","
        << std::fixed << std::setprecision(2) << to_display_distance(total_distance) << ","
        << runtime_ms << ","
        << csv_escape(tours_to_string(tours)) << "\n";
}

inline void write_combined_row(
    std::ofstream& out,
    const std::string& scenario,
    const std::string& dataset,
    long long total_distance_sum,
    double total_time_ms_sum
) {
    if (!out.is_open()) return;
    out << scenario << ","
        << dataset << ","
        << std::fixed << std::setprecision(2) << to_display_distance(total_distance_sum) << ","
        << total_time_ms_sum << "\n";
}

inline void append_result_csv(
    const std::string& csv_path,
    const std::string& scenario,
    const std::string& test_name,
    int total_distance,
    double runtime_ms,
    const std::vector<std::vector<int>>& tours
) {
    const std::filesystem::path p(csv_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    const bool write_header = !std::filesystem::exists(p);
    std::ofstream out(csv_path, std::ios::app);
    if (!out.is_open()) return;

    if (write_header) {
        out << "scenario,test_name,total_distance,runtime_ms,tours\n";
    }

    out << csv_escape(scenario) << ","
        << csv_escape(test_name) << ","
        << std::fixed << std::setprecision(2) << to_display_distance(total_distance) << ","
        << runtime_ms << ","
        << csv_escape(tours_to_string(tours)) << "\n";
}

} // namespace mcvrp::utils
