#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mcvrp::ca {

struct Dataset {
    std::vector<std::pair<double, double>> vehicle_coords;
    std::vector<std::pair<double, double>> target_coords;
    std::vector<int> weights;
};

struct BestBid {
    int depot = -1;
    double bid = 0.0;
    std::vector<int> subset;
};

struct SolutionResult {
    std::vector<BestBid> selected;
    double total_cost = 0.0;
    std::string status;
};

struct DatasetResult {
    int id = 0;
    std::string status;
    double cost = 0.0;
    double time_sec = 0.0;
    int feasible_subsets = 0;
    int total_subsets = 0;
};

struct SubsetTSP {
    bool feasible = false;
    double internal = 0.0;
    int first_idx = 0;
    int last_idx = 0;
};

struct BidResult {
    std::vector<std::vector<double>> bid_matrix;
    std::vector<std::vector<int>> subsets;
    std::vector<int> feasible_subsets;
};

} // namespace mcvrp::ca
