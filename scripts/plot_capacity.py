#!/usr/bin/env python3
"""Plot the capacity-sweep results.

Reads results/capacity/summary.csv (tidy: capacity,method,scenario,total_cost,
mean_cost,total_time_ms,n_datasets,status,capacity_valid) and draws, per scenario,
one line per method of cost-vs-capacity and time-vs-capacity. Capacity is a swept
continuous parameter, so a line plot (not a box plot) is the right view here.

Rows are dropped when:
  * status is not "ok"          -> timed-out/failed runs leave a gap, and
  * capacity_valid is "false"   -> capacities that are invalid for that scenario.
The "wi <= C/2" constraint is only satisfiable once C >= 2*maxDemand, so the
weight-constrained scenarios (scenario1, scenario2) are plotted only over their
valid capacity range; scenario3 (K = 2, no weight limit) keeps every capacity. The
capacity_valid flag is computed by run_capacity_tests.sh from the real max demand.
Older summaries without the column are treated as all-valid (no filtering).

Outputs (under results/capacity/plots/):
  scenarioN_capacity_cost.png
  scenarioN_capacity_time.png
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--results-root", type=Path, default=repo_root / "results" / "capacity",
                   help="Directory holding summary.csv (default: results/capacity)")
    p.add_argument("--out-dir", type=Path, default=None,
                   help="Where to write plots (default: <results-root>/plots)")
    p.add_argument("--cost-metric", choices=["mean_cost", "total_cost"], default="mean_cost",
                   help="Which cost column to plot (default: mean_cost)")
    p.add_argument("--linear-time", action="store_true",
                   help="Use a linear y-axis for time (default: log, since times span orders of magnitude)")
    args = p.parse_args()
    if args.out_dir is None:
        args.out_dir = args.results_root / "plots"
    return args


# scenario -> method -> sorted list of (capacity, cost, time_ms)
def read_summary(path: Path) -> Dict[str, Dict[str, List[Tuple[float, float, float]]]]:
    if not path.exists():
        raise SystemExit(f"summary not found: {path}\nRun scripts/run_capacity_tests.sh first.")
    data: Dict[str, Dict[str, List[Tuple[float, float, float]]]] = {}
    with path.open("r", newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if (row.get("status") or "").strip() != "ok":
                continue
            # Drop capacities flagged invalid for this scenario (wi <= C/2 not
            # satisfiable). Missing/empty column -> older summary -> treat as valid.
            if (row.get("capacity_valid") or "true").strip().lower() in ("false", "0", "no"):
                continue
            sc = row["scenario"].strip()
            method = row["method"].strip()
            cap = float(row["capacity"])
            entry = (cap, float(row["mean_cost"]), float(row["total_cost"]), float(row["total_time_ms"]))
            data.setdefault(sc, {}).setdefault(method, []).append(entry)
    for sc in data:
        for method in data[sc]:
            data[sc][method].sort(key=lambda t: t[0])
    return data


def make_line_plot(out_path: Path, series: Dict[str, List[Tuple]], value_idx: int,
                   ylabel: str, title: str, log_y: bool) -> None:
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(1, 1, figsize=(11, 6.5))
    for method in sorted(series):
        pts = series[method]
        x = [p[0] for p in pts]
        y = [p[value_idx] for p in pts]
        ax.plot(x, y, marker="o", linewidth=1.8, label=method)

    ax.set_xlabel("Capacity")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log_y:
        ax.set_yscale("log")
    ax.grid(True, alpha=0.3, which="both")
    ax.legend(loc="best")

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

    data = read_summary(args.results_root / "summary.csv")
    if not data:
        raise SystemExit("No ok rows in summary.csv — nothing to plot.")

    # entry tuple layout: (capacity, mean_cost, total_cost, total_time_ms)
    cost_idx = 1 if args.cost_metric == "mean_cost" else 2
    cost_label = "Mean cost per dataset" if args.cost_metric == "mean_cost" else "Total cost"

    written: List[str] = []
    for sc in sorted(data):
        series = data[sc]

        cost_path = args.out_dir / f"{sc}_capacity_cost.png"
        make_line_plot(cost_path, series, cost_idx, cost_label,
                       f"{sc}: cost vs capacity", log_y=False)
        written.append(str(cost_path))

        time_path = args.out_dir / f"{sc}_capacity_time.png"
        make_line_plot(time_path, series, 3, "Total runtime (ms)",
                       f"{sc}: runtime vs capacity", log_y=not args.linear_time)
        written.append(str(time_path))

    print("Wrote capacity plots:\n  " + "\n  ".join(written))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
