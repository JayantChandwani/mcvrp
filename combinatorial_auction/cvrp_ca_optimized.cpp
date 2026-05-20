#include <bits/stdc++.h>
#include <omp.h>
#include "ortools/linear_solver/linear_solver.h"

using namespace std;
using namespace operations_research;

// ============================================================
// OPTIMIZED C++ CVRP Combinatorial Auction Solver
// Key optimizations vs previous version:
//   1) Distance matrix precomputed ONCE per dataset (like numpy)
//   2) TSP internal route cached per subset — NOT recomputed per depot
//   3) Depot legs computed in a tight vectorized loop
//   4) All 100 datasets processed in parallel (outer OpenMP loop)
//   5) TSP cache computation parallelized across subsets
// ============================================================

struct Dataset {
    vector<pair<double,double>> vehicle_coords;
    vector<pair<double,double>> target_coords;
    vector<int> weights;
};

struct BestBid {
    int depot; double bid; vector<int> subset;
};

struct SolutionResult {
    vector<BestBid> selected; double total_cost; string status;
};

struct DatasetResult {
    int id; string status; double cost; double time_sec;
};

// ---------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------
string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == string::npos) ? "" : s.substr(a, b - a + 1);
}

pair<double,double> parse_coord(const string& token) {
    string t = trim(token);
    auto c = t.find(',');
    return { stod(t.substr(0, c)), stod(t.substr(c + 1)) };
}

// ---------------------------------------------------------------
// Parser
// ---------------------------------------------------------------
map<int, Dataset> parse_cvrp_data(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) { cerr << "Cannot open: " << filename << "\n"; exit(1); }
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    map<int, Dataset> datasets;
    string marker = "Data set #";
    size_t pos = 0;

    while (true) {
        size_t start = content.find(marker, pos);
        if (start == string::npos) break;
        start += marker.size();
        size_t next = content.find(marker, start);
        string block = (next != string::npos)
                     ? content.substr(start, next - start)
                     : content.substr(start);

        istringstream ss(block);
        string line;
        getline(ss, line);
        int dataset_num = stoi(trim(line));
        Dataset dataset;

        auto parse_coord_line = [&](const string& l) {
            string cs = l.substr(l.find(':') + 1);
            cs = trim(cs);
            if (!cs.empty() && cs.back() == ';') cs.pop_back();
            vector<pair<double,double>> coords;
            istringstream cst(cs); string tok;
            while (getline(cst, tok, ';')) {
                tok = trim(tok);
                if (!tok.empty()) coords.push_back(parse_coord(tok));
            }
            return coords;
        };

        while (getline(ss, line)) {
            line = trim(line);
            if (line.empty()) continue;
            if (line.find("Vehicle locations") != string::npos)
                dataset.vehicle_coords = parse_coord_line(line);
            else if (line.find("Target locations") != string::npos)
                dataset.target_coords = parse_coord_line(line);
            else if (line.find("Weights") != string::npos) {
                string ws = trim(line.substr(line.find('=') + 1));
                istringstream wst(ws); string tok;
                while (getline(wst, tok, ',')) {
                    tok = trim(tok);
                    if (!tok.empty()) dataset.weights.push_back(stoi(tok));
                }
            }
        }
        datasets[dataset_num] = dataset;
        pos = (next != string::npos) ? next : string::npos;
        if (pos == string::npos) break;
    }
    return datasets;
}

// ---------------------------------------------------------------
// Precompute full distance matrix (V+T x V+T)
// Index: 0..V-1 = depots, V..V+T-1 = targets
// ---------------------------------------------------------------
vector<vector<double>> build_dist_matrix(
    const vector<pair<double,double>>& vc,
    const vector<pair<double,double>>& tc)
{
    int V = vc.size(), T = tc.size(), N = V + T;
    vector<pair<double,double>> all(N);
    for (int i = 0; i < V; i++) all[i]     = vc[i];
    for (int i = 0; i < T; i++) all[V + i] = tc[i];

    vector<vector<double>> dm(N, vector<double>(N, 0.0));
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++) {
            double dx = all[i].first  - all[j].first;
            double dy = all[i].second - all[j].second;
            double d  = sqrt(dx*dx + dy*dy);
            dm[i][j] = d; dm[j][i] = d;
        }
    return dm;
}

// ---------------------------------------------------------------
// Subset Generation (combinations)
// ---------------------------------------------------------------
void comb_helper(int n, int sz, int start,
                 vector<int>& cur, vector<vector<int>>& res) {
    if ((int)cur.size() == sz) { res.push_back(cur); return; }
    for (int i = start; i < n; i++) {
        cur.push_back(i);
        comb_helper(n, sz, i + 1, cur, res);
        cur.pop_back();
    }
}

vector<vector<int>> generate_subsets(int n, int max_size = 3) {
    vector<vector<int>> subsets;
    for (int sz = 1; sz <= min(max_size, n); sz++) {
        vector<int> cur;
        comb_helper(n, sz, 0, cur, subsets);
    }
    return subsets;
}

// ---------------------------------------------------------------
// TSP Cache: compute internal route ONCE per subset
// ---------------------------------------------------------------
struct SubsetTSP {
    bool   feasible  = false;
    double internal  = 0.0;
    int    first_idx = 0;   // absolute index in distance matrix
    int    last_idx  = 0;
};

vector<SubsetTSP> precompute_tsp_cache(
    const vector<vector<int>>& subsets,
    const vector<vector<double>>& dm,
    int V,
    const vector<int>& weights,
    int capacity = 100)
{
    int S = subsets.size();
    vector<SubsetTSP> cache(S);

    #pragma omp parallel for schedule(dynamic, 128)
    for (int s = 0; s < S; s++) {
        const auto& sub = subsets[s];
        int tw = 0;
        for (int i : sub) tw += weights[i];
        if (tw > capacity) continue;

        cache[s].feasible = true;
        vector<int> abs(sub.size());
        for (int i = 0; i < (int)sub.size(); i++) abs[i] = V + sub[i];
        int n = abs.size();

        if (n == 1) {
            cache[s].first_idx = abs[0];
            cache[s].last_idx  = abs[0];
            cache[s].internal  = 0.0;
        } else if (n == 2) {
            cache[s].first_idx = abs[0];
            cache[s].last_idx  = abs[1];
            cache[s].internal  = dm[abs[0]][abs[1]];
        } else {
            // All 6 permutations for size-3
            vector<int> perm = abs;
            sort(perm.begin(), perm.end());
            double best = 1e18;
            int bf = perm[0], bl = perm[n-1];
            do {
                double cost = 0;
                for (int i = 0; i < n - 1; i++) cost += dm[perm[i]][perm[i+1]];
                if (cost < best) { best = cost; bf = perm[0]; bl = perm[n-1]; }
            } while (next_permutation(perm.begin(), perm.end()));
            cache[s].internal  = best;
            cache[s].first_idx = bf;
            cache[s].last_idx  = bl;
        }
    }
    return cache;
}

// ---------------------------------------------------------------
// Build bid matrix using cached TSP
// bid[d][s] = dm[d][first] + internal + dm[last][d]
// ---------------------------------------------------------------
struct BidResult {
    vector<vector<double>> bid_matrix;
    vector<vector<int>>    subsets;
    vector<int>            feasible_subsets;
};

BidResult generate_bids_matrix(const Dataset& dataset, int max_subset_size = 3) {
    int V = dataset.vehicle_coords.size();
    int T = dataset.target_coords.size();

    auto subsets   = generate_subsets(T, max_subset_size);
    int  S         = subsets.size();
    auto dm        = build_dist_matrix(dataset.vehicle_coords, dataset.target_coords);
    auto tsp_cache = precompute_tsp_cache(subsets, dm, V, dataset.weights);

    vector<int> feasible;
    feasible.reserve(S);
    for (int s = 0; s < S; s++)
        if (tsp_cache[s].feasible) feasible.push_back(s);

    int F = feasible.size();
    vector<vector<double>> bid_matrix(V, vector<double>(S, INFINITY));

    // For each depot, vectorized loop over feasible subsets
    #pragma omp parallel for schedule(static)
    for (int d = 0; d < V; d++) {
        for (int fi = 0; fi < F; fi++) {
            int s = feasible[fi];
            const auto& tc = tsp_cache[s];
            bid_matrix[d][s] = dm[d][tc.first_idx]
                              + tc.internal
                              + dm[tc.last_idx][d];
        }
    }

    return {bid_matrix, subsets, feasible};
}

// ---------------------------------------------------------------
// Best Bids
// ---------------------------------------------------------------
map<int, BestBid> find_best_bids(
    const vector<vector<double>>& bm,
    const vector<vector<int>>& subsets,
    const vector<int>& feasible)
{
    map<int, BestBid> best;
    int V = bm.size();
    for (int s : feasible) {
        double bv = INFINITY; int bd = -1;
        for (int d = 0; d < V; d++)
            if (bm[d][s] < bv) { bv = bm[d][s]; bd = d; }
        if (bv < INFINITY) best[s] = {bd, bv, subsets[s]};
    }
    return best;
}

// ---------------------------------------------------------------
// Set Partitioning (OR-Tools CBC)
// ---------------------------------------------------------------
SolutionResult solve_set_partitioning(
    const map<int, BestBid>& best_bids, int n_targets)
{
    MPSolver solver("SPP", MPSolver::CBC_MIXED_INTEGER_PROGRAMMING);
    solver.SuppressOutput();

    unordered_map<int, MPVariable*> x;
    for (auto& [id, bid] : best_bids)
        x[id] = solver.MakeBoolVar("x_" + to_string(id));

    auto* obj = solver.MutableObjective();
    for (auto& [id, bid] : best_bids)
        obj->SetCoefficient(x[id], bid.bid);
    obj->SetMinimization();

    vector<vector<int>> tmap(n_targets);
    for (auto& [id, bid] : best_bids)
        for (int t : bid.subset) tmap[t].push_back(id);

    for (int t = 0; t < n_targets; t++) {
        if (tmap[t].empty()) return {{}, 0, "Uncoverable"};
        auto* c = solver.MakeRowConstraint(1, 1);
        for (int s : tmap[t]) c->SetCoefficient(x[s], 1);
    }

    if (solver.Solve() == MPSolver::OPTIMAL) {
        vector<BestBid> sol; double cost = 0;
        for (auto& [id, bid] : best_bids)
            if (x[id]->solution_value() > 0.5) { sol.push_back(bid); cost += bid.bid; }
        return {sol, cost, "Optimal"};
    }
    return {{}, 0, "Failed"};
}

// ---------------------------------------------------------------
// Process single dataset
// ---------------------------------------------------------------
DatasetResult process_dataset(int id, const Dataset& dataset) {
    auto t0  = chrono::high_resolution_clock::now();
    auto bids = generate_bids_matrix(dataset, 3);
    auto best = find_best_bids(bids.bid_matrix, bids.subsets, bids.feasible_subsets);
    auto sol  = solve_set_partitioning(best, (int)dataset.target_coords.size());
    double elapsed = chrono::duration<double>(chrono::high_resolution_clock::now() - t0).count();
    return {id, sol.status, sol.total_cost, elapsed};
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------
int main() {
    string filename = "CVRP_10Vehicles_100Targets.txt";

    auto datasets = parse_cvrp_data(filename);
    int ncpus = omp_get_max_threads();
    cout << "Loaded " << datasets.size() << " datasets | CPUs: " << ncpus << "\n\n";

    vector<int> ids;
    for (int i = 1; i <= 100; i++)
        if (datasets.count(i)) ids.push_back(i);
    int N = ids.size();
    vector<DatasetResult> results(N);

    auto wall_start = chrono::high_resolution_clock::now();

    // Outer parallelism: datasets processed in parallel
    #pragma omp parallel for schedule(dynamic) num_threads(ncpus)
    for (int i = 0; i < N; i++)
        results[i] = process_dataset(ids[i], datasets[ids[i]]);

    double wall = chrono::duration<double>(
        chrono::high_resolution_clock::now() - wall_start).count();

    sort(results.begin(), results.end(), [](auto& a, auto& b){ return a.id < b.id; });

    cout << left  << setw(12) << "Dataset"
         << setw(12) << "Status"
         << right << setw(14) << "Cost"
         << setw(10) << "Time(s)" << "\n"
         << string(52, '-') << "\n";

    int ok = 0; double cost_sum = 0;
    for (auto& r : results) {
        string cs = (r.cost > 0) ? (ostringstream() << fixed << setprecision(2) << r.cost).str() : "N/A";
        cout << left  << setw(12) << r.id
             << setw(12) << r.status
             << right << setw(14) << cs
             << setw(9)  << fixed << setprecision(3) << r.time_sec << "s\n";
        if (r.status == "Optimal") { ok++; cost_sum += r.cost; }
    }

    cout << "\nTotal wall-clock time : " << fixed << setprecision(2) << wall << "s\n";
    cout << "Datasets solved       : " << ok << " / " << N << "\n";
    if (ok) cout << "Average cost          : " << fixed << setprecision(2) << cost_sum/ok << "\n";
    return 0;
}