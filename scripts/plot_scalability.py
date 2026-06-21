#!/usr/bin/env python3
"""Plot the scalability results.

The scalability run pushes one large dataset family (1000 targets / 20 depots,
50 datasets) through every method/scenario at default capacity. Each
<method>_<scenario>.csv is a per-dataset combined CSV
(scenario,dataset,total_distance_sum,total_time_ms_sum), so per scenario we draw
one box per method showing the distribution of cost and of runtime across the 50
datasets — mirroring the compare_* box plots. Methods that timed out have no CSV
and are simply omitted (noted on the figure).

Outputs (under results/scalability/plots/):
  scenarioN_scalability_cost.png
  scenarioN_scalability_time.png
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple


METHOD_ORDER = ["cluster_first", "match_first", "ca_ilp", "ca_mis"]
SCENARIOS = ["scenario1", "scenario2", "scenario3"]


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--results-root", type=Path, default=repo_root / "results" / "scalability",
                   help="Directory holding <method>_<scenario>.csv (default: results/scalability)")
    p.add_argument("--out-dir", type=Path, default=None,
                   help="Where to write plots (default: <results-root>/plots)")
    p.add_argument("--linear-time", action="store_true",
                   help="Use a linear y-axis for time (default: log, since times span orders of magnitude)")
    args = p.parse_args()
    if args.out_dir is None:
        args.out_dir = args.results_root / "plots"
    return args


def read_series(path: Path) -> Tuple[List[float], List[float]]:
    costs: List[float] = []
    times: List[float] = []
    with path.open("r", newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            try:
                costs.append(float(row["total_distance_sum"]))
                times.append(float(row["total_time_ms_sum"]))
            except (KeyError, ValueError):
                continue
    return costs, times


def make_box_plot(out_path: Path, labels: List[str], data: List[List[float]],
                  ylabel: str, title: str, missing: List[str], log_y: bool) -> None:
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(1, 1, figsize=(11, 6.5))
    ax.boxplot(data, tick_labels=labels, showmeans=True)

    ax.set_xlabel("Method")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log_y:
        ax.set_yscale("log")
    ax.grid(True, axis="y", alpha=0.3, which="both")
    ax.tick_params(axis="x", rotation=20)
    if missing:
        ax.text(0.99, 0.01, "omitted (timeout/no data): " + ", ".join(missing),
                transform=ax.transAxes, ha="right", va="bottom", fontsize=8, color="gray")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=170)
    plt.close(fig)


def main() -> int:
    args = parse_args()
    try:
        import matplotlib  # noqa: F401
    except ImportError as exc:
        raise SystemExit("matplotlib is required. Install with: pip install matplotlib") from exc

    written: List[str] = []
    for sc in SCENARIOS:
        labels: List[str] = []
        cost_data: List[List[float]] = []
        time_data: List[List[float]] = []
        missing: List[str] = []
        sc_num = sc[-1]
        for method in METHOD_ORDER:
            path = args.results_root / f"{method}_{sc}.csv"
            if not path.exists():
                missing.append(method)
                continue
            costs, times = read_series(path)
            if not costs:
                missing.append(method)
                continue
            labels.append(method)
            cost_data.append(costs)
            time_data.append(times)

        if not labels:
            print(f"{sc}: no data, skipping.")
            continue

        cost_path = args.out_dir / f"{sc}_scalability_cost.png"
        make_box_plot(cost_path, labels, cost_data, "Total distance",
                      f"{sc}: cost distribution at scale (n={len(cost_data[0])} datasets)",
                      missing, log_y=False)
        written.append(str(cost_path))

        time_path = args.out_dir / f"{sc}_scalability_time.png"
        make_box_plot(time_path, labels, time_data, "Runtime (ms)",
                      f"{sc}: runtime distribution at scale",
                      missing, log_y=not args.linear_time)
        written.append(str(time_path))

    if not written:
        raise SystemExit("No scalability CSVs found — run scripts/run_scalability_test.sh first.")
    print("Wrote scalability plots:\n  " + "\n  ".join(written))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
