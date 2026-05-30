#include "datasets_parser.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <stdexcept>
#include "../utils/utils.hpp"

namespace mcvrp::parser {

namespace {
inline std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<mcvrp::types::Coordinates> parse_point_list_strict(const std::string& s) {
    std::vector<mcvrp::types::Coordinates> out;
    std::istringstream iss(s);
    std::string cur;
    while (std::getline(iss, cur, ';')) {
        cur = trim(cur);
        if (cur.empty()) continue;
        auto comma = cur.find(',');
        if (comma == std::string::npos) {
            throw std::runtime_error("Invalid coordinate token: " + cur);
        }
        double x = std::stod(trim(cur.substr(0, comma)));
        double y = std::stod(trim(cur.substr(comma + 1)));
        out.push_back({x, y});
    }
    return out;
}

std::vector<int> parse_int_list_strict(const std::string& s) {
    std::vector<int> out;
    std::istringstream iss(s);
    std::string cur;
    while (std::getline(iss, cur, ',')) {
        cur = trim(cur);
        if (cur.empty()) continue;
        out.push_back(std::stoi(cur));
    }
    return out;
}

} // namespace

std::vector<Dataset> parse_datasets_txt_raw(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open datasets file: " + filepath);
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }

    std::vector<Dataset> datasets;
    std::size_t i = 0;
    while (i < lines.size()) {
        std::string cur = trim(lines[i]);
        if (cur.empty()) {
            ++i;
            continue;
        }

        if (cur.rfind("Data set #", 0) != 0) {
            ++i;
            continue;
        }

        const auto hash_pos = cur.find('#');
        if (hash_pos == std::string::npos) {
            throw std::runtime_error("Invalid dataset header: " + cur);
        }
        const int dataset_idx = std::stoi(trim(cur.substr(hash_pos + 1)));

        if (i + 3 >= lines.size()) {
            throw std::runtime_error("Dataset #" + std::to_string(dataset_idx) + ": incomplete block at end of file.");
        }

        const std::string vehicles_line = trim(lines[i + 1]);
        const std::string targets_line = trim(lines[i + 2]);
        const std::string weights_line = trim(lines[i + 3]);

        if (vehicles_line.rfind("Vehicle locations :", 0) != 0) {
            throw std::runtime_error("Dataset #" + std::to_string(dataset_idx) + ": missing 'Vehicle locations :'.");
        }
        if (targets_line.rfind("Target locations :", 0) != 0) {
            throw std::runtime_error("Dataset #" + std::to_string(dataset_idx) + ": missing 'Target locations :'.");
        }
        if (weights_line.rfind("Weights =", 0) != 0) {
            throw std::runtime_error("Dataset #" + std::to_string(dataset_idx) + ": missing 'Weights ='.");
        }

        const auto depots_raw = vehicles_line.substr(vehicles_line.find(':') + 1);
        const auto targets_raw = targets_line.substr(targets_line.find(':') + 1);
        const auto weights_raw = weights_line.substr(weights_line.find('=') + 1);

        Dataset d;
        d.index = dataset_idx;
        d.depots = parse_point_list_strict(depots_raw);
        d.targets = parse_point_list_strict(targets_raw);
        d.weights = parse_int_list_strict(weights_raw);

        if (d.targets.size() != d.weights.size()) {
            throw std::runtime_error(
                "Dataset #" + std::to_string(dataset_idx) + ": target/weight count mismatch (" +
                std::to_string(d.targets.size()) + " targets vs " + std::to_string(d.weights.size()) + " weights)."
            );
        }

        datasets.push_back(std::move(d));
        i += 4;
    }

    if (datasets.empty()) {
        throw std::runtime_error("No datasets found in: " + filepath);
    }

    return datasets;
}

} // namespace mcvrp::parser
