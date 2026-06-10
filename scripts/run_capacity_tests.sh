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
#                                    (scenario1 has no capacity constraint: run
#                                     without the flag -> a flat reference line)
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
# Capacities to sweep, increasing. With max demand = 100 in datasets.txt, the
# "wi <= C/2" constraint is satisfied for every target once C >= 200; below that
# the heaviest targets fall out to singleton routes. Pushing C much past ~500
# makes scenario2's exact group ordering (factorial) expensive -- raise with care.
CAPACITIES=(150 200 250 300 400)
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

build="$repo_root/build"
results_dir="$repo_root/results/capacity"
summary="$results_dir/summary.csv"
rm -rf "$results_dir"
mkdir -p "$results_dir/raw"

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
            [ "$sc" != "1" ] && args+=(--capacity "$cap")    # scenario1 takes no --capacity
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

# Sum total_distance_sum (col 3) and total_time_ms_sum (col 4) over a combined CSV.
# Echoes: "<total_cost> <mean_cost> <total_time_ms> <n>"
aggregate() {
    awk -F, 'NR>1 && $3!="" { c+=$3; t+=$4; n++ }
             END { if (n>0) printf "%.4f %.4f %.4f %d", c, c/n, t, n; else printf "NA NA NA 0" }' "$1"
}

echo "capacity,method,scenario,total_cost,mean_cost,total_time_ms,n_datasets,status" > "$summary"

# Which methods are actually available (ca_ilp needs OR-Tools at build time).
methods=("${BLOSSOM[@]}")
for m in "${CA[@]}"; do
    if [ -x "$build/$m/${m}_main" ]; then methods+=("$m"); else echo "==> skipping $m (binary not built)"; fi
done

for cap in "${CAPACITIES[@]}"; do
    rawdir="$results_dir/raw/cap_${cap}"
    mkdir -p "$rawdir"
    echo "==> capacity = $cap"
    for method in "${methods[@]}"; do
        for sc in "${SCENARIOS[@]}"; do
            combined="$build/output/$method/scenario$sc/sc_${sc}_combined.csv"
            rm -f "$combined"
            run_one "$method" "$sc" "$cap"; rc=$?
            # timeout(1) exits 124 on TERM, 137 when -k escalates to KILL.
            if   [ $rc -eq 124 ] || [ $rc -eq 137 ]; then status=timeout
            elif [ $rc -ne 0 ]               || [ ! -f "$combined" ]; then status=failed
            else status=ok; fi

            if [ "$status" = ok ]; then
                read -r tcost mcost ttime n <<<"$(aggregate "$combined")"
                cp "$combined" "$rawdir/${method}_scenario${sc}.csv"
            else
                tcost=NA mcost=NA ttime=NA n=0
            fi
            printf '%s,%s,scenario%s,%s,%s,%s,%s,%s\n' \
                "$cap" "$method" "$sc" "$tcost" "$mcost" "$ttime" "$n" "$status" >> "$summary"
            printf '    %-14s scenario%s  cost=%-12s time_ms=%-12s [%s]\n' \
                "$method" "$sc" "$tcost" "$ttime" "$status"
        done
    done
done

# Split the tidy summary into one plot-ready CSV per (method, scenario) series.
awk -F, 'NR==1 { next }
         { f = dir "/series_" $2 "_" $3 ".csv"
           if (!(f in seen)) { print "capacity,total_cost,mean_cost,total_time_ms,n_datasets,status" > f; seen[f]=1 }
           print $1","$4","$5","$6","$7","$8 >> f }' \
    dir="$results_dir" "$summary"

echo "==> Done. Results in $results_dir"
echo "    summary.csv (tidy), series_<method>_scenarioN.csv (per-line), raw/cap_<C>/ (per-dataset)"
