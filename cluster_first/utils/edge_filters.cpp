#include "edge_filters.hpp"
#include <unordered_map>

namespace mcvrp::filters {

types::GraphInput apply_weight_constraint(const types::GraphInput& input) {
    types::GraphInput filtered = input;
    filtered.edges.clear();
    filtered.edges.reserve(input.edges.size());

    std::unordered_map<int, int> demand_by_id;
    demand_by_id.reserve(input.nodes.size());
    for (const auto& node : input.nodes) {
        demand_by_id[node.id] = node.demand;
    }

    for (const auto& [u_id, v_id, w] : input.edges) {
        auto u_it = demand_by_id.find(u_id);
        auto v_it = demand_by_id.find(v_id);
        if (u_it == demand_by_id.end() || v_it == demand_by_id.end()) {
            continue;
        }

        if (u_it->second + v_it->second <= input.capacity) {
            filtered.edges.push_back({u_id, v_id, w});
        }
    }

    return filtered;
}

} // namespace mcvrp::filters
