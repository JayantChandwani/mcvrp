#pragma once

#include "../utils/types.hpp"
#include <map>
#include <string>

namespace mcvrp::ca::parser {

std::map<int, Dataset> parse_cvrp_data(const std::string& filename);

} // namespace mcvrp::ca::parser
