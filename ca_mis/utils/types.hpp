#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mcvrp::ca_mis {

struct Dataset {
    std::vector<std::pair<double, double>> vehicle_coords;
    std::vector<std::pair<double, double>> target_coords;
    std::vector<int> weights;
};

struct BestBid {
    int depot = -1;
    double bid = 0.0;
    std::vector<int> subset;
    std::vector<int> order;
};

struct SubsetTSP {
    bool feasible = false;
    double internal = 0.0;
    int first_idx = 0;
    int last_idx = 0;
    std::vector<int> order;
};

struct CandidateBids {
    std::vector<BestBid> bids;
    int feasible_subsets = 0;
    int total_subsets = 0;
};

struct MISResult {
    std::vector<BestBid> selected;
    double total_cost = 0.0;
    std::string status;
};

struct DatasetResult {
    int id = 0;
    double cost = 0.0;
    double time_sec = 0.0;
    std::string status;
    int feasible_subsets = 0;
    int total_subsets = 0;
    std::vector<BestBid> selected;
};

} // namespace mcvrp::ca_mis
