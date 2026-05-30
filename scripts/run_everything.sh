#!/usr/bin/env bash
#
# Build everything, run all experiments for every method/scenario, then
# generate the comparison plots. Outputs land under build/output/.
#
# Usage: scripts/run_everything.sh [path/to/datasets.txt]
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
datasets="${1:-$repo_root/datasets.txt}"

echo "==> Building"
cmake -S . -B build
cmake --build build -j

# cluster_first / match_first: ./scenarioN <datasets.txt>
run_matching() {
    local method="$1"
    for s in 1 2 3; do
        echo "==> $method scenario$s"
        ( cd "build/$method" && "./scenario$s" "$datasets" )
    done
}

# ca_ilp / ca_mis: ./<bin> --input <datasets> --datasets 1-100 --scenario scenarioN
run_ca() {
    local method="$1" bin="$2"
    if [ ! -x "build/$method/$bin" ]; then
        echo "==> skipping $method ($bin not built)"
        return
    fi
    for s in 1 2 3; do
        echo "==> $method scenario$s"
        ( cd "build/$method" && "./$bin" --input "$datasets" --datasets 1-100 --scenario "scenario$s" )
    done
}

run_matching cluster_first
run_matching match_first
run_ca ca_ilp ca_ilp_main
run_ca ca_mis ca_mis_main

echo "==> Plotting"
py=python3
[ -x .venv/bin/python ] && py=.venv/bin/python

# Compare/plot only the methods that actually produced output.
methods="cluster_first,match_first"
[ -x build/ca_ilp/ca_ilp_main ] && methods="$methods,ca_ilp"
[ -x build/ca_mis/ca_mis_main ] && methods="$methods,ca_mis"

"$py" scripts/compare_costs.py --methods "$methods"
"$py" scripts/compare_times.py --methods "$methods"
"$py" scripts/plot_tours.py --methods "$methods"

echo "==> Done. Outputs in build/output/"
