#include <lemon/list_graph.h>
#include <lemon/matching.h>
#include <lemon/maps.h>
#include <iostream>

int main() {
    lemon::ListGraph g;

    // Add nodes
    auto n1 = g.addNode();
    auto n2 = g.addNode();
    auto n3 = g.addNode();
    auto n4 = g.addNode();

    // Add edges
    auto e1 = g.addEdge(n1, n2);
    auto e2 = g.addEdge(n1, n3);
    auto e3 = g.addEdge(n2, n4);
    auto e4 = g.addEdge(n3, n4);

    // Assign weights to edges
    lemon::ListGraph::EdgeMap<int> weight(g);
    weight[e1] = 10;
    weight[e2] = 5;
    weight[e3] = 2;
    weight[e4] = 8;

    // Run weighted matching
    lemon::MaxWeightedMatching<lemon::ListGraph, lemon::ListGraph::EdgeMap<int>> matching(g, weight);
    matching.run();

    // Output results
    std::cout << "Matching edges:\n";
    for (lemon::ListGraph::EdgeIt e(g); e != lemon::INVALID; ++e) {
        if (matching.matching(e)) {
            std::cout << g.id(g.u(e)) << " - " << g.id(g.v(e))
                      << " (weight " << weight[e] << ")\n";
        }
    }

    std::cout << "Total matching weight: " << matching.matchingWeight() << "\n";

    return 0;
}
