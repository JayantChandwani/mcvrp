#pragma once
#include <tuple>
#include <utility>
#include <vector>
#include <string>

namespace mcvrp::types {

struct Coordinates {
    double x = 0.0;
    double y = 0.0;
};

struct Node {
    int id = 0;
    int demand = 0;
    Coordinates coordinates{};
};

struct GraphInput {
    std::vector<Node> nodes;
    std::vector<std::tuple<int, int, int>> edges; // (u, v, w_scaled_2dp)
    int capacity = 0;
};

struct MatchingResult {
    std::vector<std::pair<int, int>> matched_edges;
    std::vector<int> edge_weights; // scaled by 100
    int total_weight = 0;          // scaled by 100
};

struct TestResult {
    std::string test_name;
    int total_weight = 0;   // scaled by 100
    std::size_t num_edges = 0;
    double runtime_ms = 0.0;
};

} // namespace mcvrp::types