#pragma once

#include <lemon/smart_graph.h>
#include <vector>
#include <unordered_map>
#include "utils/types.hpp"

namespace mcvrp::graph {

class Graph {
public:
    explicit Graph(const mcvrp::types::GraphInput& graph_input);

    const lemon::SmartGraph& get_graph() const;
    const lemon::SmartGraph::EdgeMap<int>& get_weights() const;

    const std::vector<std::vector<int>>& get_distance_matrix() const;
    const std::unordered_map<int, std::size_t>& get_id_to_index() const;
    const std::vector<int>& get_index_to_id() const;

    std::size_t index_of(int node_id) const;
    int node_id_of(std::size_t index) const;
    std::size_t index_of_node(lemon::SmartGraph::Node n) const;

    int distance_by_index(std::size_t i, std::size_t j) const;
    int distance_by_id(int u_id, int v_id) const;

private:
    void build_indices(const mcvrp::types::GraphInput& graph_input);
    void build_distance_matrix(const mcvrp::types::GraphInput& graph_input);
    void build_graph(const mcvrp::types::GraphInput& graph_input);

    lemon::SmartGraph G;
    lemon::SmartGraph::EdgeMap<int> edge_weights;
    lemon::SmartGraph::NodeMap<std::size_t> node_to_index;

    std::unordered_map<int, std::size_t> id_to_index;
    std::vector<int> index_to_id;
    std::vector<lemon::SmartGraph::Node> lemon_nodes;
    std::vector<char> in_matching_graph;
    std::vector<std::vector<int>> distance_matrix;
};

} // namespace mcvrp::graph