#include "json_parser.hpp"
#include "../utils/utils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

using mcvrp::types::GraphInput;

namespace mcvrp::parser {

GraphInput parse_graph_json(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }

    nlohmann::json j;
    in >> j;

    mcvrp::types::GraphInput input;

    // Nodes: {id, demand, coordinates:{x,y}}  (supports legacy too)
    for (const auto& n : j.at("nodes")) {
        mcvrp::types::Node node;
        node.id = n.at("id").get<int>();
        node.demand = n.value("demand", 0);

        if (n.contains("coordinates")) {
            const auto& c = n.at("coordinates");
            if (c.is_object()) {
                node.coordinates.x = c.at("x").get<double>();
                node.coordinates.y = c.at("y").get<double>();
            } else if (c.is_array() && c.size() >= 2) {
                node.coordinates.x = c[0].get<double>();
                node.coordinates.y = c[1].get<double>();
            }
        }

        input.nodes.push_back(node);
    }

    std::unordered_map<int, mcvrp::types::Coordinates> coord_by_id;
    coord_by_id.reserve(input.nodes.size());
    for (const auto& n : input.nodes) {
        coord_by_id[n.id] = n.coordinates;
    }

    // Edges: new {u,v} (compute w) OR legacy {u,v,w}
    for (const auto& e : j.at("edges")) {
        const int u = e.at("u").get<int>();
        const int v = e.at("v").get<int>();

        int w = 0;
        if (e.contains("w")) {
            // legacy numeric weight -> keep (assume already scaled/int)
            w = e.at("w").get<int>();
        } else {
            const auto u_it = coord_by_id.find(u);
            const auto v_it = coord_by_id.find(v);
            if (u_it == coord_by_id.end() || v_it == coord_by_id.end()) {
                throw std::runtime_error("Missing coordinates for edge endpoint in: " + filepath);
            }
            w = mcvrp::utils::euclidean_distance_2dp_scaled(u_it->second, v_it->second);
        }

        input.edges.push_back({u, v, w});
    }

    if (j.contains("capacity")) {
        input.capacity = j.at("capacity").get<int>();
    }

    return input;
}

} // namespace mcvrp::parser