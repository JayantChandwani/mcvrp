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
    capacity_cost.png
    capacity_time.png
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple

from plot_style import color_for_method, configure_matplotlib, marker_for_method, method_label, scenario_label, title


SCENARIOS = ["scenario1", "scenario2", "scenario3"]


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


def make_line_plot(ax, series: Dict[str, List[Tuple]], value_idx: int,
                   ylabel: str, title_text: str, log_y: bool) -> None:
    for method in sorted(series):
        pts = series[method]
        x = [p[0] for p in pts]
        y = [p[value_idx] for p in pts]
        ax.plot(
            x,
            y,
            marker=marker_for_method(method),
            linewidth=2.0,
            color=color_for_method(method),
            label=method_label(method),
            markersize=6,
            markerfacecolor=color_for_method(method),
            markeredgecolor="#111111",
            markeredgewidth=0.8,
        )

    title(ax, title_text, pad=4)
    if log_y:
        ax.set_yscale("log")


def add_figure_legend(fig, axes) -> None:
    method_order = ["cluster_first", "match_first", "ca_ilp", "ca_mis"]
    handles = []
    labels = []
    seen = set()
    for ax in axes:
        handle_list, label_list = ax.get_legend_handles_labels()
        for handle, label in zip(handle_list, label_list):
            if label in seen:
                continue
            seen.add(label)
            handles.append(handle)
            labels.append(label)

    ordered_handles = []
    ordered_labels = []
    for method in method_order:
        display_label = method_label(method)
        if display_label in seen:
            index = labels.index(display_label)
            ordered_handles.append(handles[index])
            ordered_labels.append(display_label)

    if ordered_handles:
        fig.legend(
            ordered_handles,
            ordered_labels,
            loc="upper center",
            bbox_to_anchor=(0.5, 0.905),
            ncol=len(ordered_labels),
            frameon=False,
            handlelength=2.8,
            columnspacing=1.4,
        )


def main() -> int:
    args = parse_args()
    try:
        import matplotlib  # noqa: F401
    except ImportError as exc:
        raise SystemExit("matplotlib is required. Install with: pip install matplotlib") from exc

    configure_matplotlib()

    data = read_summary(args.results_root / "summary.csv")
    if not data:
        raise SystemExit("No ok rows in summary.csv — nothing to plot.")

    # entry tuple layout: (capacity, mean_cost, total_cost, total_time_ms)
    cost_idx = 1 if args.cost_metric == "mean_cost" else 2
    cost_label = "Mean Cost per Dataset" if args.cost_metric == "mean_cost" else "Total Cost"

    import matplotlib.pyplot as plt

    args.out_dir.mkdir(parents=True, exist_ok=True)
    written: List[str] = []

    fig, axes = plt.subplots(1, len(SCENARIOS), figsize=(18, 6.5))
    axes = [axes] if len(SCENARIOS) == 1 else list(axes)
    for ax, sc in zip(axes, SCENARIOS):
        series = data.get(sc, {})
        if series:
            make_line_plot(ax, series, cost_idx, cost_label, scenario_label(sc), log_y=False)
        else:
            ax.set_axis_off()
            title(ax, scenario_label(sc), pad=8)
        fig.suptitle(f"{cost_label} vs Capacity", fontsize=16, y=0.94, fontweight="semibold")
    add_figure_legend(fig, axes)
    fig.supxlabel("Capacity", y=0.033)
    fig.supylabel(cost_label, x=0.01)
    fig.tight_layout(rect=(0, 0, 1, 0.915))
    cost_path = args.out_dir / "capacity_cost.png"
    fig.savefig(cost_path, dpi=170)
    plt.close(fig)
    written.append(str(cost_path))

    fig, axes = plt.subplots(1, len(SCENARIOS), figsize=(18, 6.5))
    axes = [axes] if len(SCENARIOS) == 1 else list(axes)
    for ax, sc in zip(axes, SCENARIOS):
        series = data.get(sc, {})
        if series:
            make_line_plot(ax, series, 3, "Total Runtime (ms)", scenario_label(sc), log_y=not args.linear_time)
        else:
            ax.set_axis_off()
            title(ax, scenario_label(sc), pad=8)
        fig.suptitle("Total Runtime vs Capacity", fontsize=16, y=0.94, fontweight="semibold")
    add_figure_legend(fig, axes)
    fig.supxlabel("Capacity", y=0.033)
    fig.supylabel("Total Runtime (ms)", x=0.01)
    fig.tight_layout(rect=(0, 0, 1, 0.915))
    time_path = args.out_dir / "capacity_time.png"
    fig.savefig(time_path, dpi=170)
    plt.close(fig)
    written.append(str(time_path))

    print("Wrote capacity plots:\n  " + "\n  ".join(written))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
