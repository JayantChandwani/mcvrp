#!/usr/bin/env bash
#
# Scalability test.
#
# Run the large 1000-target / 20-depot dataset (datasets_scale.txt, 50 datasets)
# through every method and scenario once, at each method's default capacity, and
# capture per-dataset cost + runtime. The point is wall-clock scaling, so the
# per-dataset combined CSVs (which carry total_time_ms_sum) are copied out as-is,
# one file per series, plus a roll-up summary.
#
# scenario2's exact group ordering is factorial in group size, so a per-run
# timeout guards against a single method stalling the whole sweep; a method that
# times out is recorded as such and the run continues.
#
# Outputs (under results/scalability/):
#   <method>_scenarioN.csv   per-dataset cost+time for that series (plot: dataset vs time)
#   summary.csv              one row per series: totals, means, status
#
# Usage: scripts/run_scalability_test.sh [path/to/datasets_scale.txt]
set -uo pipefail   # NOT -e: a timed-out method must not abort the rest

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

# -------------------- configuration --------------------
TIMEOUT_SECS="${TIMEOUT_SECS:-1800}"         # per-invocation guard; 0 disables
BLOSSOM=(cluster_first match_first)
CA=(ca_ilp ca_mis)
SCENARIOS=(1 2 3)
# -------------------------------------------------------

datasets="${1:-$repo_root/datasets_scale.txt}"
datasets="$(cd "$(dirname "$datasets")" && pwd)/$(basename "$datasets")"
if [ ! -f "$datasets" ]; then
    echo "datasets file not found: $datasets" >&2
    echo "generate it with: python3 scripts/generate_datasets.py --out datasets_scale.txt --datasets 50 --depots 20 --targets 1000 --seed 7" >&2
    exit 1
fi
nds="$(grep -c '^Data set #' "$datasets")"

build="$repo_root/build"
results_dir="$repo_root/results/scalability"
summary="$results_dir/summary.csv"
rm -rf "$results_dir"
mkdir -p "$results_dir"

echo "==> Building"
cmake -S "$repo_root" -B "$build" >/dev/null
cmake --build "$build" -j

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

# run_one <method> <scenario_num>  (default capacity, no --capacity override)
run_one() {
    local method="$1" sc="$2" rc=0
    case "$method" in
        cluster_first|match_first)
            ( cd "$build/$method" && run_guarded "./scenario$sc" "$datasets" ) >/dev/null 2>&1 || rc=$?
            ;;
        ca_ilp|ca_mis)
            ( cd "$build/$method" && run_guarded "./${method}_main" \
                --input "$datasets" --datasets "1-$nds" --scenario "scenario$sc" \
            ) >/dev/null 2>&1 || rc=$?
            ;;
    esac
    return $rc
}

# Echoes: "<total_cost> <mean_cost> <total_time_ms> <mean_time_ms> <n>"
aggregate() {
    awk -F, 'NR>1 && $3!="" { c+=$3; t+=$4; n++ }
             END { if (n>0) printf "%.4f %.4f %.4f %.4f %d", c, c/n, t, t/n, n;
                   else printf "NA NA NA NA 0" }' "$1"
}

echo "method,scenario,n_datasets,total_cost,mean_cost,total_time_ms,mean_time_ms,status" > "$summary"

methods=("${BLOSSOM[@]}")
for m in "${CA[@]}"; do
    if [ -x "$build/$m/${m}_main" ]; then methods+=("$m"); else echo "==> skipping $m (binary not built)"; fi
done

echo "==> Running $nds datasets through ${#methods[@]} methods x ${#SCENARIOS[@]} scenarios"
for method in "${methods[@]}"; do
    for sc in "${SCENARIOS[@]}"; do
        combined="$build/output/$method/scenario$sc/sc_${sc}_combined.csv"
        rm -f "$combined"
        start=$SECONDS
        run_one "$method" "$sc"; rc=$?
        # timeout(1) exits 124 on TERM, 137 when -k escalates to KILL.
        if   [ $rc -eq 124 ] || [ $rc -eq 137 ]; then status=timeout
        elif [ $rc -ne 0 ]               || [ ! -f "$combined" ]; then status=failed
        else status=ok; fi
        elapsed=$(( SECONDS - start ))

        if [ "$status" = ok ]; then
            read -r tcost mcost ttime mtime n <<<"$(aggregate "$combined")"
            cp "$combined" "$results_dir/${method}_scenario${sc}.csv"
        else
            tcost=NA mcost=NA ttime=NA mtime=NA n=0
        fi
        printf '%s,scenario%s,%s,%s,%s,%s,%s,%s\n' \
            "$method" "$sc" "$n" "$tcost" "$mcost" "$ttime" "$mtime" "$status" >> "$summary"
        printf '    %-14s scenario%s  mean_time_ms=%-12s wall=%ss [%s]\n' \
            "$method" "$sc" "$mtime" "$elapsed" "$status"
    done
done

echo "==> Done. Results in $results_dir"
echo "    <method>_scenarioN.csv (per-dataset cost+time), summary.csv (roll-up)"
