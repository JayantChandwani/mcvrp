#include "solver.hpp"
#include <lemon/matching.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

using mcvrp::types::MatchingResult;
using mcvrp::graph::Graph;

namespace mcvrp::solver {

Solver::Solver() {
    // Empty constructor - no state to initialize
}

MatchingResult Solver::get_minimal_weighted_matching(Graph& G) {
    MatchingResult result;

    const auto& graph = G.get_graph();
    const auto& weights = G.get_weights();

    int max_weight = 0;
    for (lemon::SmartGraph::EdgeIt e(graph); e != lemon::INVALID; ++e) {
        max_weight = std::max(max_weight, weights[e]);
    }

    lemon::SmartGraph::EdgeMap<int> shifted_weights(graph);
    for (lemon::SmartGraph::EdgeIt e(graph); e != lemon::INVALID; ++e) {
        shifted_weights[e] = (max_weight + 1) - weights[e];
    }

    lemon::MaxWeightedMatching<lemon::SmartGraph> mwm(graph, shifted_weights);
    mwm.run();

    long long matching_sum = 0;
    for (lemon::SmartGraph::EdgeIt e(graph); e != lemon::INVALID; ++e) {
        if (!mwm.matching(e)) continue;

        const std::size_t u_idx = G.index_of_node(graph.u(e));
        const std::size_t v_idx = G.index_of_node(graph.v(e));

        const int u_id = G.node_id_of(u_idx);
        const int v_id = G.node_id_of(v_idx);
        const int w = weights[e];

        result.matched_edges.push_back({u_id, v_id});
        result.edge_weights.push_back(w);
        matching_sum += w;
    }

    if (matching_sum > std::numeric_limits<int>::max()) {
        throw std::overflow_error("Matching total exceeds int range.");
    }

    result.total_weight = static_cast<int>(matching_sum);
    return result;
}

} // namespace mcvrp::solver
