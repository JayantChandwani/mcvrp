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
    scalability_cost.png
    scalability_time.png
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple

from plot_style import colorize_boxplot, configure_matplotlib, method_label, scenario_label, title


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


def make_box_plot(ax, labels: List[str], color_keys: List[str], data: List[List[float]],
                  title_text: str, log_y: bool) -> None:
    boxplot = ax.boxplot(data, tick_labels=labels, showmeans=True, widths=0.6, patch_artist=True)
    colorize_boxplot(boxplot, color_keys)

    title(ax, title_text, pad=8)
    if log_y:
        ax.set_yscale("log")
    ax.tick_params(axis="x", rotation=20)


def collect_series(results_root: Path, sc: str) -> Tuple[List[str], List[str], List[List[float]], List[List[float]], List[str]]:
    labels: List[str] = []
    color_keys: List[str] = []
    cost_data: List[List[float]] = []
    time_data: List[List[float]] = []
    missing: List[str] = []

    for method in METHOD_ORDER:
        path = results_root / f"{method}_{sc}.csv"
        if not path.exists():
            missing.append(method)
            continue
        costs, times = read_series(path)
        if not costs or not times:
            missing.append(method)
            continue
        labels.append(method_label(method))
        color_keys.append(method)
        cost_data.append(costs)
        time_data.append(times)

    return labels, color_keys, cost_data, time_data, missing


def main() -> int:
    args = parse_args()
    try:
        import matplotlib  # noqa: F401
    except ImportError as exc:
        raise SystemExit("matplotlib is required. Install with: pip install matplotlib") from exc

    configure_matplotlib()

    import matplotlib.pyplot as plt

    args.out_dir.mkdir(parents=True, exist_ok=True)
    written: List[str] = []

    fig, axes = plt.subplots(1, len(SCENARIOS), figsize=(18, 6.5))
    axes = [axes] if len(SCENARIOS) == 1 else list(axes)
    for ax, sc in zip(axes, SCENARIOS):
        labels, color_keys, cost_data, _time_data, missing = collect_series(args.results_root, sc)
        if not labels:
            ax.set_axis_off()
            title(ax, scenario_label(sc), pad=8)
            continue
        make_box_plot(ax, labels, color_keys, cost_data, scenario_label(sc), log_y=False)
    fig.suptitle("Total Distance Distribution at Scale", fontsize=16, y=0.94, fontweight="semibold")
    fig.supxlabel("Method", y=0.033)
    fig.supylabel("Total Distance", x=0.01)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    cost_path = args.out_dir / "scalability_cost.png"
    fig.savefig(cost_path, dpi=170)
    plt.close(fig)
    written.append(str(cost_path))

    fig, axes = plt.subplots(1, len(SCENARIOS), figsize=(18, 6.5))
    axes = [axes] if len(SCENARIOS) == 1 else list(axes)
    for ax, sc in zip(axes, SCENARIOS):
        labels, color_keys, _cost_data, time_data, missing = collect_series(args.results_root, sc)
        if not labels:
            ax.set_axis_off()
            title(ax, scenario_label(sc), pad=8)
            continue
        make_box_plot(ax, labels, color_keys, time_data, scenario_label(sc), log_y=not args.linear_time)
    fig.suptitle("Total Runtime Distribution at Scale", fontsize=16, y=0.94, fontweight="semibold")
    fig.supxlabel("Method", y=0.033)
    fig.supylabel("Total Runtime (ms)", x=0.01)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    time_path = args.out_dir / "scalability_time.png"
    fig.savefig(time_path, dpi=170)
    plt.close(fig)
    written.append(str(time_path))

    if not written:
        raise SystemExit("No scalability CSVs found — run scripts/run_scalability_test.sh first.")
    print("Wrote scalability plots:\n  " + "\n  ".join(written))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
