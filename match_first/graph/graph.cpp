#include "graph/graph.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

using mcvrp::types::GraphInput;

namespace mcvrp::graph {

namespace {
constexpr int INF = std::numeric_limits<int>::max() / 4;
constexpr int SOURCE_ID = 0;
}

Graph::Graph(const GraphInput& graph_input)
    : edge_weights(G), node_to_index(G) {
    build_indices(graph_input);
    build_distance_matrix(graph_input);
    build_graph(graph_input);
}

void Graph::build_indices(const GraphInput& graph_input) {
    id_to_index.clear();
    index_to_id.clear();
    index_to_id.reserve(graph_input.nodes.size());

    for (std::size_t i = 0; i < graph_input.nodes.size(); ++i) {
        const int id = graph_input.nodes[i].id;
        id_to_index[id] = i;
        index_to_id.push_back(id);
    }
}

void Graph::build_distance_matrix(const GraphInput& graph_input) {
    const std::size_t n = index_to_id.size();
    distance_matrix.assign(n, std::vector<int>(n, INF));

    for (std::size_t i = 0; i < n; ++i) {
        distance_matrix[i][i] = 0;
    }

    for (const auto& [u_id, v_id, w] : graph_input.edges) {
        const auto u_it = id_to_index.find(u_id);
        const auto v_it = id_to_index.find(v_id);
        if (u_it == id_to_index.end() || v_it == id_to_index.end()) {
            continue;
        }

        const std::size_t u = u_it->second;
        const std::size_t v = v_it->second;

        // Keep minimum in case of duplicate edges.
        distance_matrix[u][v] = std::min(distance_matrix[u][v], w);
        distance_matrix[v][u] = std::min(distance_matrix[v][u], w);
    }
}

void Graph::build_graph(const GraphInput& graph_input) {
    lemon_nodes.assign(index_to_id.size(), lemon::INVALID);
    in_matching_graph.assign(index_to_id.size(), 0);

    for (std::size_t i = 0; i < index_to_id.size(); ++i) {
        if (index_to_id[i] == SOURCE_ID) continue; // exclude source from matching graph
        auto n = G.addNode();
        lemon_nodes[i] = n;
        in_matching_graph[i] = 1;
        node_to_index[n] = i;
    }

    for (const auto& [u_id, v_id, w] : graph_input.edges) {
        const auto u_it = id_to_index.find(u_id);
        const auto v_it = id_to_index.find(v_id);
        if (u_it == id_to_index.end() || v_it == id_to_index.end()) {
            continue;
        }

        const std::size_t u = u_it->second;
        const std::size_t v = v_it->second;
        if (!in_matching_graph[u] || !in_matching_graph[v]) {
            continue; // drop depot-adjacent edges for matching
        }

        auto edge = G.addEdge(lemon_nodes[u], lemon_nodes[v]);
        edge_weights[edge] = w;
    }
}

const lemon::SmartGraph& Graph::get_graph() const {
    return G;
}

const lemon::SmartGraph::EdgeMap<int>& Graph::get_weights() const {
    return edge_weights;
}

const std::vector<std::vector<int>>& Graph::get_distance_matrix() const {
    return distance_matrix;
}

const std::unordered_map<int, std::size_t>& Graph::get_id_to_index() const {
    return id_to_index;
}

const std::vector<int>& Graph::get_index_to_id() const {
    return index_to_id;
}

std::size_t Graph::index_of(int node_id) const {
    const auto it = id_to_index.find(node_id);
    if (it == id_to_index.end()) {
        throw std::out_of_range("node_id not found in Graph::index_of");
    }
    return it->second;
}

int Graph::node_id_of(std::size_t index) const {
    if (index >= index_to_id.size()) {
        throw std::out_of_range("index out of range in Graph::node_id_of");
    }
    return index_to_id[index];
}

int Graph::distance_by_index(std::size_t i, std::size_t j) const {
    if (i >= distance_matrix.size() || j >= distance_matrix.size()) {
        throw std::out_of_range("index out of range in Graph::distance_by_index");
    }
    return distance_matrix[i][j];
}

int Graph::distance_by_id(int u_id, int v_id) const {
    return distance_by_index(index_of(u_id), index_of(v_id));
}

std::size_t Graph::index_of_node(lemon::SmartGraph::Node n) const {
    return node_to_index[n];
}

} // namespace mcvrp::graph