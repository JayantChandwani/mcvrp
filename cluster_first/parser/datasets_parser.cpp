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

double squared_distance(const mcvrp::types::Coordinates& a, const mcvrp::types::Coordinates& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

std::vector<std::vector<int>> assign_targets_to_depots(const mcvrp::parser::Dataset& dataset) {
    std::vector<std::vector<int>> clusters(dataset.depots.size());
    if (dataset.depots.empty()) return clusters;

    for (std::size_t t_idx = 0; t_idx < dataset.targets.size(); ++t_idx) {
        int best_depot_idx = 0;
        double best_dist = squared_distance(dataset.targets[t_idx], dataset.depots[0]);

        for (std::size_t d_idx = 1; d_idx < dataset.depots.size(); ++d_idx) {
            double dist = squared_distance(dataset.targets[t_idx], dataset.depots[d_idx]);
            if (dist < best_dist) {
                best_dist = dist;
                best_depot_idx = static_cast<int>(d_idx);
            }
        }

        clusters[best_depot_idx].push_back(static_cast<int>(t_idx));
    }

    return clusters;
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

std::vector<std::pair<std::string, mcvrp::types::GraphInput>> build_cluster_graphs(
    const Dataset& dataset,
    bool keep_empty
) {
    std::vector<std::pair<std::string, mcvrp::types::GraphInput>> out;
    const auto clusters = assign_targets_to_depots(dataset);

    for (std::size_t depot_idx = 0; depot_idx < clusters.size(); ++depot_idx) {
        const auto& target_indices = clusters[depot_idx];
        if (target_indices.empty() && !keep_empty) {
            continue;
        }

        mcvrp::types::GraphInput g;
        mcvrp::types::Node depot;
        depot.id = 0;
        depot.demand = 0;
        depot.coordinates = dataset.depots[depot_idx];
        g.nodes.push_back(depot);

        int next_id = 1;
        for (int target_idx : target_indices) {
            mcvrp::types::Node n;
            n.id = next_id++;
            n.demand = dataset.weights[target_idx];
            n.coordinates = dataset.targets[target_idx];
            g.nodes.push_back(n);
        }

        const std::size_t non_depot = (g.nodes.size() >= 1) ? (g.nodes.size() - 1) : 0;
        if ((non_depot % 2) == 1) {
            mcvrp::types::Node dummy;
            dummy.id = next_id++;
            dummy.demand = 0;
            dummy.coordinates = dataset.depots[depot_idx];
            g.nodes.push_back(dummy);
        }

        for (std::size_t i = 0; i < g.nodes.size(); ++i) {
            for (std::size_t j = i + 1; j < g.nodes.size(); ++j) {
                const int w = mcvrp::utils::euclidean_distance_2dp_scaled(
                    g.nodes[i].coordinates,
                    g.nodes[j].coordinates
                );
                g.edges.push_back({g.nodes[i].id, g.nodes[j].id, w});
            }
        }

        std::ostringstream name;
        name << "cluster_" << std::setw(2) << std::setfill('0') << (depot_idx + 1) << ".json";
        out.emplace_back(name.str(), std::move(g));
    }

    return out;
}

} // namespace mcvrp::parser
