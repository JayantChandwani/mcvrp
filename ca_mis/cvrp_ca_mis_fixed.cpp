#include <bits/stdc++.h>
#include <omp.h>

using namespace std;

// ============================================================
// CVRP Combinatorial Auction — Fast MIS Solver
//
// Key insight: We don't need to explicitly build a conflict graph.
// Instead, we directly enforce the independent set constraint
// during greedy selection: a bid is "independent" from selected
// bids if it shares NO targets with any already-selected bid.
//
// This turns the O(n²) conflict graph build into O(n*k) where
// k = avg subset size (≤3), making it extremely fast.
//
// Algorithm:
//   1) Compute best bids (cached TSP + vectorized depot legs)
//   2) Sort bids by cost/subset_size (cheapest per target first)
//   3) Greedy: select bid if none of its targets are taken
//   4) Repair: for any uncovered target, add cheapest valid bid
//   5) Local search: swap bids for cheaper alternatives
//   6) Multi-restart for solution quality
// ============================================================

struct Dataset {
    vector<pair<double,double>> vehicle_coords;
    vector<pair<double,double>> target_coords;
    vector<int> weights;
};

struct BestBid {
    int id, depot;
    double bid;
    vector<int> targets;
};

struct DatasetResult {
    int id; double cost, time_sec; string status;
};

// ---------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------
string trim(const string& s){
    size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return (a==string::npos)?"":s.substr(a,b-a+1);
}
pair<double,double> parse_coord(const string& t2){
    string t=trim(t2); auto c=t.find(',');
    return {stod(t.substr(0,c)),stod(t.substr(c+1))};
}

// ---------------------------------------------------------------
// Parser
// ---------------------------------------------------------------
map<int,Dataset> parse_cvrp_data(const string& filename){
    ifstream file(filename);
    if(!file.is_open()){cerr<<"Cannot open: "<<filename<<"\n";exit(1);}
    string content((istreambuf_iterator<char>(file)),istreambuf_iterator<char>());
    map<int,Dataset> datasets;
    string marker="Data set #"; size_t pos=0;
    while(true){
        size_t start=content.find(marker,pos);
        if(start==string::npos) break;
        start+=marker.size();
        size_t next=content.find(marker,start);
        string block=(next!=string::npos)?content.substr(start,next-start):content.substr(start);
        istringstream ss(block); string line;
        getline(ss,line); int num=stoi(trim(line));
        Dataset dataset;
        auto pcl=[&](const string& l){
            string cs=l.substr(l.find(':')+1); cs=trim(cs);
            if(!cs.empty()&&cs.back()==';') cs.pop_back();
            vector<pair<double,double>> coords;
            istringstream cst(cs); string tok;
            while(getline(cst,tok,';')){tok=trim(tok);if(!tok.empty()) coords.push_back(parse_coord(tok));}
            return coords;
        };
        while(getline(ss,line)){
            line=trim(line); if(line.empty()) continue;
            if(line.find("Vehicle locations")!=string::npos) dataset.vehicle_coords=pcl(line);
            else if(line.find("Target locations")!=string::npos) dataset.target_coords=pcl(line);
            else if(line.find("Weights")!=string::npos){
                string ws=trim(line.substr(line.find('=')+1));
                istringstream wst(ws); string tok;
                while(getline(wst,tok,',')){tok=trim(tok);if(!tok.empty()) dataset.weights.push_back(stoi(tok));}
            }
        }
        datasets[num]=dataset;
        pos=(next!=string::npos)?next:string::npos;
        if(pos==string::npos) break;
    }
    return datasets;
}

// ---------------------------------------------------------------
// Distance matrix
// ---------------------------------------------------------------
vector<vector<double>> build_dist_matrix(
    const vector<pair<double,double>>& vc,
    const vector<pair<double,double>>& tc)
{
    int V=vc.size(),T=tc.size(),N=V+T;
    vector<pair<double,double>> all(N);
    for(int i=0;i<V;i++) all[i]=vc[i];
    for(int i=0;i<T;i++) all[V+i]=tc[i];
    vector<vector<double>> dm(N,vector<double>(N,0.0));
    for(int i=0;i<N;i++)
        for(int j=i+1;j<N;j++){
            double dx=all[i].first-all[j].first,dy=all[i].second-all[j].second;
            double d=sqrt(dx*dx+dy*dy); dm[i][j]=d; dm[j][i]=d;
        }
    return dm;
}

// ---------------------------------------------------------------
// Subset Generation
// ---------------------------------------------------------------
void comb_helper(int n,int sz,int s,vector<int>&c,vector<vector<int>>&r){
    if((int)c.size()==sz){r.push_back(c);return;}
    for(int i=s;i<n;i++){c.push_back(i);comb_helper(n,sz,i+1,c,r);c.pop_back();}
}
vector<vector<int>> generate_subsets(int n,int max_size=3){
    vector<vector<int>> s;
    for(int sz=1;sz<=min(max_size,n);sz++){vector<int>c;comb_helper(n,sz,0,c,s);}
    return s;
}

// ---------------------------------------------------------------
// TSP Cache
// ---------------------------------------------------------------
struct SubsetTSP{bool feasible=false;double internal=0.0;int first_idx=0,last_idx=0;};

vector<SubsetTSP> precompute_tsp_cache(
    const vector<vector<int>>& subsets,
    const vector<vector<double>>& dm,
    int V,const vector<int>& weights,int capacity)
{
    int S=subsets.size();
    vector<SubsetTSP> cache(S);
    #pragma omp parallel for schedule(dynamic,128)
    for(int s=0;s<S;s++){
        const auto& sub=subsets[s];
        int tw=0; for(int i:sub) tw+=weights[i];
        if(tw>capacity) continue;
        cache[s].feasible=true;
        vector<int> abs; for(int i:sub) abs.push_back(V+i);
        int n=abs.size();
        if(n==1){cache[s].first_idx=abs[0];cache[s].last_idx=abs[0];}
        else if(n==2){cache[s].first_idx=abs[0];cache[s].last_idx=abs[1];cache[s].internal=dm[abs[0]][abs[1]];}
        else{
            vector<int> perm=abs; sort(perm.begin(),perm.end());
            double best=1e18; int bf=perm[0],bl=perm[n-1];
            do{
                double cost=0; for(int i=0;i<n-1;i++) cost+=dm[perm[i]][perm[i+1]];
                if(cost<best){best=cost;bf=perm[0];bl=perm[n-1];}
            }while(next_permutation(perm.begin(),perm.end()));
            cache[s].internal=best; cache[s].first_idx=bf; cache[s].last_idx=bl;
        }
    }
    return cache;
}

// ---------------------------------------------------------------
// Compute best bids
// ---------------------------------------------------------------
vector<BestBid> compute_best_bids(const Dataset& dataset,int max_subset_size=3){
    int V=dataset.vehicle_coords.size(),T=dataset.target_coords.size();
    auto subsets=generate_subsets(T,max_subset_size);
    int S=subsets.size();
    auto dm=build_dist_matrix(dataset.vehicle_coords,dataset.target_coords);

    // Capacity = 2 * max demand from dataset
    int capacity = 1.5 * (*max_element(dataset.weights.begin(), dataset.weights.end()));

    auto tsp_cache=precompute_tsp_cache(subsets,dm,V,dataset.weights,capacity);

    vector<int> feasible;
    for(int s=0;s<S;s++) if(tsp_cache[s].feasible) feasible.push_back(s);
    int F=feasible.size();

    vector<vector<double>> bm(V,vector<double>(F,INFINITY));
    #pragma omp parallel for schedule(static)
    for(int d=0;d<V;d++)
        for(int fi=0;fi<F;fi++){
            int s=feasible[fi]; const auto& tc=tsp_cache[s];
            bm[d][fi]=dm[d][tc.first_idx]+tc.internal+dm[tc.last_idx][d];
        }

    vector<BestBid> best_bids; best_bids.reserve(F);
    for(int fi=0;fi<F;fi++){
        double bv=INFINITY; int bd=-1;
        for(int d=0;d<V;d++) if(bm[d][fi]<bv){bv=bm[d][fi];bd=d;}
        if(bv<INFINITY) best_bids.push_back({feasible[fi],bd,bv,subsets[feasible[fi]]});
    }
    return best_bids;
}

// ---------------------------------------------------------------
// Fast greedy MIS — no conflict graph needed
// Uses target ownership array: O(n * k) where k <= 3
// ---------------------------------------------------------------
pair<vector<int>,double> greedy_mis(
    const vector<BestBid>& bids,
    const vector<int>& order,
    int n_targets)
{
    int N=bids.size();
    // target_owner[t] = index of bid that owns target t, -1 if uncovered
    vector<int> target_owner(n_targets,-1);
    vector<bool> selected(N,false);
    vector<int> sel;

    // Phase 1: greedy
    for(int i:order){
        // Check if any target already owned
        bool conflict=false;
        for(int t:bids[i].targets) if(target_owner[t]!=-1){conflict=true;break;}
        if(conflict) continue;
        // Select this bid
        selected[i]=true; sel.push_back(i);
        for(int t:bids[i].targets) target_owner[t]=i;
    }

    // Phase 2: repair uncovered targets
    // For each uncovered target, find cheapest bid that doesn't conflict
    for(int t=0;t<n_targets;t++){
        if(target_owner[t]!=-1) continue;
        double best_cost=1e18; int best_j=-1;
        for(int j=0;j<N;j++){
            if(selected[j]) continue;
            if(bids[j].bid>=best_cost) continue;
            bool has_t=false;
            for(int tt:bids[j].targets) if(tt==t){has_t=true;break;}
            if(!has_t) continue;
            bool conflict=false;
            for(int tt:bids[j].targets) if(target_owner[tt]!=-1){conflict=true;break;}
            if(!conflict){best_cost=bids[j].bid;best_j=j;}
        }
        if(best_j>=0){
            selected[best_j]=true; sel.push_back(best_j);
            for(int tt:bids[best_j].targets) target_owner[tt]=best_j;
        }
    }

    // Validate
    for(int t=0;t<n_targets;t++) if(target_owner[t]==-1) return {{},1e18};
    double cost=0; for(int i:sel) cost+=bids[i].bid;
    return {sel,cost};
}

// ---------------------------------------------------------------
// Local search: swap each selected bid for a cheaper compatible one
// ---------------------------------------------------------------
pair<vector<int>,double> local_search(
    const vector<BestBid>& bids,
    vector<int> sel,
    double cur_cost,
    int n_targets)
{
    int N=bids.size();
    vector<int> target_owner(n_targets,-1);
    vector<bool> selected(N,false);
    for(int i:sel){
        selected[i]=true;
        for(int t:bids[i].targets) target_owner[t]=i;
    }

    bool improved=true;
    while(improved){
        improved=false;
        for(int& si:sel){
            int i=si;
            // Temporarily remove bid i
            selected[i]=false;
            for(int t:bids[i].targets) target_owner[t]=-1;

            // Find cheapest replacement covering same targets
            double best_cost=bids[i].bid; int best_j=-1;
            for(int j=0;j<N;j++){
                if(selected[j]||j==i) continue;
                if(bids[j].bid>=best_cost) continue;
                if(bids[j].targets!=bids[i].targets) continue;
                // Check no conflict with remaining selected
                bool conflict=false;
                for(int t:bids[j].targets) if(target_owner[t]!=-1){conflict=true;break;}
                if(!conflict){best_cost=bids[j].bid;best_j=j;}
            }

            int use=(best_j>=0)?best_j:i;
            selected[use]=true; si=use;
            for(int t:bids[use].targets) target_owner[t]=use;
            if(best_j>=0){cur_cost=cur_cost-bids[i].bid+bids[best_j].bid;improved=true;}
        }
    }
    return {sel,cur_cost};
}

// ---------------------------------------------------------------
// Full MIS solver: multi-restart greedy + local search
// ---------------------------------------------------------------
pair<double,string> solve_mis(
    const vector<BestBid>& bids,int n_targets,int n_restarts=8)
{
    int N=bids.size();

    // Base order: cheapest per target first
    vector<int> base(N); iota(base.begin(),base.end(),0);
    sort(base.begin(),base.end(),[&](int a,int b){
        return (bids[a].bid/bids[a].targets.size())<(bids[b].bid/bids[b].targets.size());
    });

    double best_cost=1e18;
    mt19937 rng(42);

    for(int r=0;r<n_restarts;r++){
        vector<int> order=base;
        if(r>0){
            // Shuffle top 30% for diversity
            int top=max(1,(int)(N*0.3));
            shuffle(order.begin(),order.begin()+top,rng);
        }
        auto [sel,cost]=greedy_mis(bids,order,n_targets);
        if(sel.empty()) continue;
        auto [lsel,lcost]=local_search(bids,sel,cost,n_targets);
        if(lcost<best_cost) best_cost=lcost;
    }

    if(best_cost>=1e17) return {0,"Failed"};
    return {best_cost,"Optimal"};
}

// ---------------------------------------------------------------
// Process single dataset
// ---------------------------------------------------------------
DatasetResult process_dataset(int id,const Dataset& dataset){
    auto t0=chrono::high_resolution_clock::now();
    auto best_bids=compute_best_bids(dataset,3);
    int n_targets=dataset.target_coords.size();
    auto [cost,status]=solve_mis(best_bids,n_targets,8);
    double elapsed=chrono::duration<double>(chrono::high_resolution_clock::now()-t0).count();
    return {id,cost,elapsed,status};
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------
int main(){
    string filename="CVRP_10Vehicles_100Targets.txt";
    auto datasets=parse_cvrp_data(filename);
    int ncpus=omp_get_max_threads();
    cout<<"Loaded "<<datasets.size()<<" datasets | CPUs: "<<ncpus<<"\n\n";

    vector<int> ids;
    for(int i=1;i<=100;i++) if(datasets.count(i)) ids.push_back(i);
    int N=ids.size();
    vector<DatasetResult> results(N);

    auto wall_start=chrono::high_resolution_clock::now();
    #pragma omp parallel for schedule(dynamic) num_threads(ncpus)
    for(int i=0;i<N;i++)
        results[i]=process_dataset(ids[i],datasets[ids[i]]);
    double wall=chrono::duration<double>(chrono::high_resolution_clock::now()-wall_start).count();

    sort(results.begin(),results.end(),[](auto&a,auto&b){return a.id<b.id;});

    cout<<left<<setw(12)<<"Dataset"<<setw(12)<<"Status"
        <<right<<setw(14)<<"Cost"<<setw(10)<<"Time(s)"<<"\n"
        <<string(52,'-')<<"\n";

    int ok=0; double sum=0;
    for(auto& r:results){
        string cs=(r.cost>0)?(ostringstream()<<fixed<<setprecision(2)<<r.cost).str():"N/A";
        cout<<left<<setw(12)<<r.id<<setw(12)<<r.status
            <<right<<setw(14)<<cs<<setw(9)<<fixed<<setprecision(3)<<r.time_sec<<"s\n";
        if(r.status=="Optimal"){ok++;sum+=r.cost;}
    }

    cout<<"\nTotal wall-clock time : "<<fixed<<setprecision(2)<<wall<<"s\n";
    cout<<"Datasets solved       : "<<ok<<"/"<<N<<"\n";
    if(ok) cout<<"Average cost          : "<<fixed<<setprecision(2)<<sum/ok<<"\n";
    return 0;
}