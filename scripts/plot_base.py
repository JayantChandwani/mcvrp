#!/usr/bin/env python3
"""Stage and plot the 100-instance base dataset results.

This script reads the per-method/per-scenario combined CSVs produced under
build/output/<method>/<scenario>/sc_N_combined.csv, copies them into
results/base/<method>_<scenario>.csv, writes a tidy summary.csv, and then
renders one 3-column figure for cost and one for runtime.

Outputs (under results/base/):
  summary.csv
  <method>_<scenario>.csv
  plots/base_cost.png
  plots/base_time.png
"""
from __future__ import annotations

import argparse
import csv
import shutil
from pathlib import Path
from typing import Dict, List, Tuple

from plot_style import colorize_boxplot, configure_matplotlib, method_label, scenario_label, title


METHOD_ORDER = ["cluster_first", "match_first", "ca_ilp", "ca_mis"]
SCENARIOS = ["scenario1", "scenario2", "scenario3"]


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source-root", type=Path, default=repo_root / "build" / "output",
                   help="Root holding <method>/<scenario>/sc_N_combined.csv (default: build/output)")
    p.add_argument("--results-root", type=Path, default=repo_root / "results" / "base",
                   help="Where to stage CSVs and write plots (default: results/base)")
    p.add_argument("--methods", type=str, default=",".join(METHOD_ORDER),
                   help="Comma-separated methods to include")
    p.add_argument("--linear-time", action="store_true",
                   help="Use a linear y-axis for time (default: log, since times span orders of magnitude)")
    args = p.parse_args()
    return args


def parse_methods(raw: str) -> List[str]:
    return [method.strip() for method in raw.split(",") if method.strip()]


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


def copy_stage_outputs(source_root: Path, results_root: Path, methods: List[str]) -> List[Tuple[str, str, Path]]:
    staged: List[Tuple[str, str, Path]] = []
    for method in methods:
        for sc in SCENARIOS:
            source = source_root / method / sc / f"sc_{sc[-1]}_combined.csv"
            if not source.exists():
                continue
            destination = results_root / f"{method}_{sc}.csv"
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            staged.append((method, sc, destination))
    return staged


def write_summary(summary_path: Path, staged: List[Tuple[str, str, Path]]) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["method", "scenario", "n_datasets", "total_distance_sum", "mean_distance_sum", "total_time_ms_sum", "mean_time_ms_sum"])
        for method, sc, csv_path in staged:
            costs, times = read_series(csv_path)
            n = len(costs)
            total_cost = sum(costs)
            total_time = sum(times)
            mean_cost = total_cost / n if n else 0.0
            mean_time = total_time / n if n else 0.0
            writer.writerow([
                method,
                sc,
                n,
                f"{total_cost:.4f}",
                f"{mean_cost:.4f}",
                f"{total_time:.4f}",
                f"{mean_time:.4f}",
            ])


def make_box_plot(ax, labels: List[str], color_keys: List[str], data: List[List[float]], title_text: str, log_y: bool) -> None:
    boxplot = ax.boxplot(data, tick_labels=labels, showmeans=True, widths=0.6, patch_artist=True)
    colorize_boxplot(boxplot, color_keys)
    title(ax, title_text, pad=8)
    if log_y:
        ax.set_yscale("log")
    ax.tick_params(axis="x", rotation=20)


def plot_metric(results_root: Path, out_path: Path, ylabel: str, suptitle: str, value_name: str, log_y: bool) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("matplotlib is required. Install with: pip install matplotlib") from exc

    configure_matplotlib()

    fig, axes = plt.subplots(1, len(SCENARIOS), figsize=(18, 6.5))
    axes = [axes] if len(SCENARIOS) == 1 else list(axes)
    for ax, sc in zip(axes, SCENARIOS):
        labels: List[str] = []
        color_keys: List[str] = []
        data: List[List[float]] = []
        for method in METHOD_ORDER:
            csv_path = results_root / f"{method}_{sc}.csv"
            if not csv_path.exists():
                continue
            costs, times = read_series(csv_path)
            values = costs if value_name == "cost" else times
            if not values:
                continue
            labels.append(method_label(method))
            color_keys.append(method)
            data.append(values)

        if not labels:
            ax.set_axis_off()
            title(ax, scenario_label(sc), pad=8)
            continue

        make_box_plot(ax, labels, color_keys, data, scenario_label(sc), log_y)

    fig.suptitle(suptitle, fontsize=16, y=0.94, fontweight="semibold")
    fig.supxlabel("Method", y=0.033)
    fig.supylabel(ylabel, x=0.01)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=170)
    plt.close(fig)


def main() -> int:
    args = parse_args()
    methods = parse_methods(args.methods)
    if not methods:
        raise SystemExit("No methods specified.")

    args.results_root.mkdir(parents=True, exist_ok=True)
    plots_root = args.results_root / "plots"

    staged = copy_stage_outputs(args.source_root, args.results_root, methods)
    if not staged:
        raise SystemExit(
            "No combined CSVs found under build/output. Run scripts/run_everything.sh first, "
            "or point --source-root at a populated build/output tree."
        )

    write_summary(args.results_root / "summary.csv", staged)

    plot_metric(
        args.results_root,
        plots_root / "base_cost.png",
        "Total Distance",
        "Total Distance Distribution by Method",
        value_name="cost",
        log_y=False,
    )
    plot_metric(
        args.results_root,
        plots_root / "base_time.png",
        "Total Runtime (ms)",
        "Total Runtime Distribution by Method",
        value_name="time",
        log_y=not args.linear_time,
    )

    print("Wrote base outputs to:")
    print(f"  {args.results_root}")
    print(f"  {plots_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
