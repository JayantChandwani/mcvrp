#pragma once

#include "../graph/graph.hpp"
#include "types.hpp"

#include <string>
#include <vector>

namespace mcvrp::cluster_first {

struct OutputRow {
    std::string test_name;
    std::string cluster_name;
    int total = 0;
    double runtime_ms = 0.0;
    std::vector<std::vector<int>> tours;
};

int scenario2_capacity(const types::GraphInput& input); // wi <= C/2
int scenario3_capacity(const types::GraphInput& input); // wi <= C/2, K = 2

// Iterative match-and-merge clustering used by scenario 2.
std::vector<std::vector<int>> build_scenario2_clusters(const types::GraphInput& input, int capacity);

// In-cluster route construction and its length (source -> closest -> best order -> source).
std::vector<int> build_greedy_cluster_order(const std::vector<int>& cluster, const graph::Graph& base_graph, int depot_id);
long long cluster_distance(const std::vector<int>& ordered_cluster, const graph::Graph& base_graph, int depot_id);

} // namespace mcvrp::cluster_first
