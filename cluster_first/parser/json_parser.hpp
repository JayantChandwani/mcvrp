#pragma once
#include "../utils/types.hpp" 
#include <string>

namespace mcvrp::parser {

    mcvrp::types::GraphInput parse_graph_json(const std::string &filepath);

} // namespace parser