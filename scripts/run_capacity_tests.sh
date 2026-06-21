#!/usr/bin/env bash
#
# Capacity sweep.
#
# For an increasing list of capacities, run the blossom methods (match_first,
# cluster_first) and the combinatorial-auction methods (ca_ilp, ca_mis), every
# scenario, on datasets.txt. Each (method, scenario) series is written to its own
# CSV of capacity -> cost/time so plotting one line per series is trivial, and all
# scenarios for a method land on the same axes.
#
# Executable routing ("the correct executable according to the capacity") -- the
# four methods have different CLIs, so the capacity is threaded in differently:
#   match_first / cluster_first  ->  ./scenarioN <datasets> [--capacity C]
#                                    (scenario1 is wi <= C/2 / K=2 but ensures the
#                                     constraint by construction -- it takes no
#                                     --capacity flag, so it's a flat reference line.
#                                     scenario3 is K=2 only: its weight-constraint
#                                     filter drops violating edges, valid at any C.)
#   ca_ilp / ca_mis              ->  ./<bin> --input <datasets> --datasets 1-N \
#                                           --scenario scenarioN --capacity C
#
# Outputs (under results/capacity/):
#   summary.csv                       tidy: capacity,method,scenario,total_cost,...
#   series_<method>_<sc>.csv          one row per capacity -> directly plottable
#   raw/cap_<C>/<method>_<sc>.csv     full per-dataset combined CSV for each run
#
# Usage: scripts/run_capacity_tests.sh [path/to/datasets.txt]
set -uo pipefail   # deliberately NOT -e: a timed-out/failed run must not abort the sweep

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

# -------------------- configuration --------------------
# Capacities to sweep, increasing. The binding constraint is "wi <= C/2", so a
# capacity is only *valid* for a weight-constrained scenario once C >= 2*maxDemand
# (with max demand = 100 in datasets.txt that's C >= 200); below that the heaviest
# targets violate wi <= C/2 and fall out to singleton routes. We keep one sub-200
# point (150) as an illustrative invalid case (flagged capacity_valid=false in the
# summary and dropped by the plotter), and spread the rest across the valid range.
# Pushing C much past ~500 makes scenario2's exact group ordering (factorial)
# expensive -- raise with care.
#
# Which scenarios enforce wi <= C/2 (so capacity gates validity): scenario1
# (wi <= C/2, K = 2) and scenario2 (wi <= C/2, K <= 4). scenario3 is pure matching
# (K = 2, no weight limit) -> capacity is irrelevant and every capacity is valid
# for it (a flat reference line).
CAPACITIES=(30 50 90 150 200 250 300 350 400 500)
WEIGHT_CONSTRAINED_SCENARIOS=" 1 2 "      # space-padded for substring membership test
TIMEOUT_SECS="${TIMEOUT_SECS:-900}"          # per-invocation guard; 0 disables
BLOSSOM=(cluster_first match_first)
CA=(ca_ilp ca_mis)
SCENARIOS=(1 2 3)
# -------------------------------------------------------

datasets="${1:-$repo_root/datasets.txt}"
# Absolute path: the binaries run from build/<method>, so a relative path would break.
datasets="$(cd "$(dirname "$datasets")" && pwd)/$(basename "$datasets")"
if [ ! -f "$datasets" ]; then
    echo "datasets file not found: $datasets" >&2
    exit 1
fi
nds="$(grep -c '^Data set #' "$datasets")"

# Largest single-target demand across the file -> the "wi <= C/2" constraint is
# satisfiable for every target only once C >= 2*max_demand. Used to flag each row
# as capacity_valid for the weight-constrained scenarios.
max_demand="$(awk -F'[=,]' '/^Weights/ { for (i = 2; i <= NF; i++) { v = $i + 0; if (v > m) m = v } } END { print m + 0 }' "$datasets")"
min_valid_capacity=$((2 * max_demand))
echo "==> max demand = $max_demand  ->  min valid capacity (2*maxDemand) = $min_valid_capacity"

build="$repo_root/build"
results_dir="$repo_root/results/capacity"
summary="$results_dir/summary.csv"
# Incremental run: previously-completed results are cached under raw/cap_<C>/ and
# reused as-is -- a (capacity, method, scenario) is executed only when it has no
# cached result. summary.csv and the per-series CSVs are always rebuilt from
# whatever raw results exist (cached + newly run), so re-invoking just fills the
# gaps (e.g. newly added capacities) and never re-runs anything already done.
mkdir -p "$results_dir/raw"
rm -f "$summary" "$results_dir"/series_*.csv

echo "==> Building"
cmake -S "$repo_root" -B "$build" >/dev/null
cmake --build "$build" -j

# Per-invocation timeout, using whichever coreutils binary exists (gtimeout on macOS).
TIMEOUT_BIN=""
if   command -v timeout  >/dev/null 2>&1; then TIMEOUT_BIN=timeout
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_BIN=gtimeout
fi
if [ -n "$TIMEOUT_BIN" ] && [ "$TIMEOUT_SECS" -gt 0 ]; then
    echo "==> per-run timeout: $TIMEOUT_BIN ${TIMEOUT_SECS}s (SIGKILL +30s)"
else
    echo "==> note: no timeout guard active (install coreutils, or TIMEOUT_SECS=0 disables intentionally)"
fi

run_guarded() {
    if [ -n "$TIMEOUT_BIN" ] && [ "$TIMEOUT_SECS" -gt 0 ]; then
        # -k: if the solver ignores SIGTERM (e.g. OR-Tools mid-solve), SIGKILL it 30s later.
        "$TIMEOUT_BIN" -k 30s "${TIMEOUT_SECS}s" "$@"
    else
        "$@"
    fi
}

# run_one <method> <scenario_num> <capacity>  -> returns the executable's exit code
run_one() {
    local method="$1" sc="$2" cap="$3" rc=0
    case "$method" in
        cluster_first|match_first)
            local args=("$datasets")
            [ "$sc" != "1" ] && args+=(--capacity "$cap")    # scenario1 fixes its own valid capacity
            ( cd "$build/$method" && run_guarded "./scenario$sc" "${args[@]}" ) >/dev/null 2>&1 || rc=$?
            ;;
        ca_ilp|ca_mis)
            ( cd "$build/$method" && run_guarded "./${method}_main" \
                --input "$datasets" --datasets "1-$nds" --scenario "scenario$sc" --capacity "$cap" \
            ) >/dev/null 2>&1 || rc=$?
            ;;
    esac
    return $rc
}

# runnable <method> <scenario_num> -> 0 if the binary needed to produce this result
# exists (so a missing cached result can actually be filled in), 1 otherwise.
runnable() {
    case "$1" in
        cluster_first|match_first) [ -x "$build/$1/scenario$2" ] ;;
        ca_ilp|ca_mis)             [ -x "$build/$1/$1_main" ] ;;
        *) return 1 ;;
    esac
}

# Sum total_distance_sum (col 3) and total_time_ms_sum (col 4) over a combined CSV.
# Echoes: "<total_cost> <mean_cost> <total_time_ms> <n>"
aggregate() {
    awk -F, 'NR>1 && $3!="" { c+=$3; t+=$4; n++ }
             END { if (n>0) printf "%.4f %.4f %.4f %d", c, c/n, t, n; else printf "NA NA NA 0" }' "$1"
}

echo "capacity,method,scenario,total_cost,mean_cost,total_time_ms,n_datasets,status,capacity_valid" > "$summary"

# Iterate every configured method. A method with no built binary can still
# contribute its previously-cached results; only *missing* results for it are
# skipped (ca_ilp/ca_mis need OR-Tools at build time).
methods=("${BLOSSOM[@]}" "${CA[@]}")
for m in "${CA[@]}"; do
    [ -x "$build/$m/${m}_main" ] || echo "==> note: $m binary not built (cached results reused; new capacities skipped)"
done

for cap in "${CAPACITIES[@]}"; do
    rawdir="$results_dir/raw/cap_${cap}"
    mkdir -p "$rawdir"
    echo "==> capacity = $cap"
    for method in "${methods[@]}"; do
        for sc in "${SCENARIOS[@]}"; do
            cached="$rawdir/${method}_scenario${sc}.csv"
            if [ -s "$cached" ]; then
                # Already run properly -> reuse it, never re-run.
                status=ok tag=cached
                read -r tcost mcost ttime n <<<"$(aggregate "$cached")"
            elif ! runnable "$method" "$sc"; then
                # No cached result and no binary to produce one -> leave a gap.
                status=skipped tag=no-binary
                tcost=NA mcost=NA ttime=NA n=0
            else
                combined="$build/output/$method/scenario$sc/sc_${sc}_combined.csv"
                rm -f "$combined"
                run_one "$method" "$sc" "$cap"; rc=$?
                # timeout(1) exits 124 on TERM, 137 when -k escalates to KILL.
                if   [ $rc -eq 124 ] || [ $rc -eq 137 ]; then status=timeout
                elif [ $rc -ne 0 ]               || [ ! -f "$combined" ]; then status=failed
                else status=ok; fi
                tag=ran
                if [ "$status" = ok ]; then
                    read -r tcost mcost ttime n <<<"$(aggregate "$combined")"
                    cp "$combined" "$cached"
                else
                    tcost=NA mcost=NA ttime=NA n=0
                fi
            fi
            # capacity_valid: scenario3 has no weight limit (always valid); the
            # weight-constrained scenarios (1, 2) require C >= 2*max_demand for wi <= C/2.
            if [ "${WEIGHT_CONSTRAINED_SCENARIOS#*" $sc "}" = "$WEIGHT_CONSTRAINED_SCENARIOS" ] || [ "$cap" -ge "$min_valid_capacity" ]; then
                capvalid=true
            else
                capvalid=false
            fi
            printf '%s,%s,scenario%s,%s,%s,%s,%s,%s,%s\n' \
                "$cap" "$method" "$sc" "$tcost" "$mcost" "$ttime" "$n" "$status" "$capvalid" >> "$summary"
            printf '    %-14s scenario%s  cost=%-12s time_ms=%-12s [%s: %s]\n' \
                "$method" "$sc" "$tcost" "$ttime" "$status" "$tag"
        done
    done
done

# Split the tidy summary into one plot-ready CSV per (method, scenario) series.
awk -F, 'NR==1 { next }
         { f = dir "/series_" $2 "_" $3 ".csv"
           if (!(f in seen)) { print "capacity,total_cost,mean_cost,total_time_ms,n_datasets,status,capacity_valid" > f; seen[f]=1 }
           print $1","$4","$5","$6","$7","$8","$9 >> f }' \
    dir="$results_dir" "$summary"

echo "==> Done. Results in $results_dir"
echo "    summary.csv (tidy), series_<method>_scenarioN.csv (per-line), raw/cap_<C>/ (per-dataset)"
