#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(
        description="Compare runtime by dataset across methods and plot per-dataset timings."
    )
    p.add_argument("--output-root", type=Path, default=repo_root / "build" / "output")
    p.add_argument("--datasets", type=str, default="1-100", help="Dataset ids, e.g. 1-100 or 1,2,10-20")
    p.add_argument("--methods", type=str, default="cluster_first,match_first,ca_ilp,ca_mis",
                   help="Comma-separated methods to compare")
    p.add_argument("--plot", type=Path, default=repo_root / "output" / "comparison" / "methods_time_comparison.png")
    return p.parse_args()


def parse_dataset_selection(raw: str) -> List[int]:
    ids: List[int] = []
    for token in raw.split(','):
        token = token.strip()
        if not token:
            continue
        if '-' in token:
            a, b = token.split('-', 1)
            i, j = int(a), int(b)
            if i > j:
                i, j = j, i
            ids.extend(range(i, j + 1))
        else:
            ids.append(int(token))
    return sorted(set(ids))


def read_combined_runtime(csv_path: Path) -> Dict[int, float]:
    out: Dict[int, float] = {}
    if not csv_path.exists():
        return out

    with csv_path.open("r", newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        for row in r:
            name = (row.get("dataset") or "").strip()
            if not name.startswith("dataset_"):
                continue
            try:
                ds = int(name.split("_")[-1])
            except ValueError:
                continue
            out[ds] = float(row.get("total_time_ms_sum", 0.0))
    return out


def write_merged_csv(path: Path, rows: List[Tuple[int, List[float]]], labels: List[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["dataset_num", *labels])
        for ds, vals in rows:
            w.writerow([ds, *[f"{v:.6f}" for v in vals]])


def make_plot(plot_path: Path, rows: List[Tuple[int, List[float]]], labels: List[str]) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise RuntimeError("matplotlib is required. Install with: pip install matplotlib") from exc

    if not rows:
        raise RuntimeError("No common datasets to plot.")

    x = [r[0] for r in rows]
    fig, ax = plt.subplots(1, 1, figsize=(14, 7))

    for idx, label in enumerate(labels):
        y = [r[1][idx] for r in rows]
        ax.plot(x, y, label=label, linewidth=1.6)

    ax.set_xlabel("Dataset number")
    ax.set_ylabel("Runtime (ms)")
    ax.set_title("Dataset-wise runtime comparison")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best")

    plot_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(plot_path, dpi=170)


def main() -> int:
    args = parse_args()
    output_root = args.output_root.resolve()
    methods = [m.strip() for m in args.methods.split(",") if m.strip()]
    if not methods:
        print("No methods specified.")
        return 1

    datasets = parse_dataset_selection(args.datasets)
    scenario_names = ["scenario1", "scenario2", "scenario3"]
    comparison_root = output_root / "comparison"

    missing = [
        str(output_root / method / sc / f"sc_{sc[-1]}_combined.csv")
        for sc in scenario_names
        for method in methods
        if not (output_root / method / sc / f"sc_{sc[-1]}_combined.csv").exists()
    ]
    if missing:
        raise SystemExit(
            "Missing experiment output(s):\n  " + "\n  ".join(missing) +
            "\nRun the experiments first (scripts/run_everything.sh). ca_ilp requires "
            "OR-Tools — install it with scripts/install_deps.sh, or pass --methods to "
            "compare only the methods you ran."
        )

    for sc in scenario_names:
        maps = []
        for method in methods:
            combined = output_root / method / sc / f"sc_{sc[-1]}_combined.csv"
            maps.append(read_combined_runtime(combined))

        common = set(datasets)
        for m in maps:
            common &= set(m.keys())
        common = sorted(common)
        rows: List[Tuple[int, List[float]]] = []
        for ds in common:
            rows.append((ds, [m[ds] for m in maps]))

        labels = [f"{m}/{sc}" for m in methods]
        merged_csv = comparison_root / f"{sc}_time_comparison.csv"
        write_merged_csv(merged_csv, rows, labels)

        plot_path = comparison_root / f"{sc}_time_comparison.png"
        if rows:
            make_plot(plot_path, rows, labels)

    print(f"Wrote time comparison outputs to: {comparison_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
