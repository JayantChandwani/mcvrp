#include "cvrp_parser.hpp"

#include "../utils/utils.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace mcvrp::ca_mis::parser {

std::map<int, Dataset> parse_cvrp_data(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filename << "\n";
        std::exit(1);
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::map<int, Dataset> datasets;
    const std::string marker = "Data set #";
    std::size_t pos = 0;

    while (true) {
        std::size_t start = content.find(marker, pos);
        if (start == std::string::npos) break;
        start += marker.size();
        const std::size_t next = content.find(marker, start);
        const std::string block = (next != std::string::npos)
            ? content.substr(start, next - start)
            : content.substr(start);

        std::istringstream ss(block);
        std::string line;
        std::getline(ss, line);
        const int dataset_num = std::stoi(utils::trim(line));
        Dataset dataset;

        auto parse_coord_line = [&](const std::string& l) {
            std::string cs = l.substr(l.find(':') + 1);
            cs = utils::trim(cs);
            if (!cs.empty() && cs.back() == ';') cs.pop_back();
            std::vector<std::pair<double, double>> coords;
            std::istringstream cst(cs);
            std::string tok;
            while (std::getline(cst, tok, ';')) {
                tok = utils::trim(tok);
                if (!tok.empty()) coords.push_back(utils::parse_coord(tok));
            }
            return coords;
        };

        while (std::getline(ss, line)) {
            line = utils::trim(line);
            if (line.empty()) continue;
            if (line.find("Vehicle locations") != std::string::npos) {
                dataset.vehicle_coords = parse_coord_line(line);
            } else if (line.find("Target locations") != std::string::npos) {
                dataset.target_coords = parse_coord_line(line);
            } else if (line.find("Weights") != std::string::npos) {
                std::string ws = utils::trim(line.substr(line.find('=') + 1));
                std::istringstream wst(ws);
                std::string tok;
                while (std::getline(wst, tok, ',')) {
                    tok = utils::trim(tok);
                    if (!tok.empty()) dataset.weights.push_back(std::stoi(tok));
                }
            }
        }

        datasets[dataset_num] = dataset;
        pos = (next != std::string::npos) ? next : std::string::npos;
        if (pos == std::string::npos) break;
    }

    return datasets;
}

} // namespace mcvrp::ca_mis::parser
