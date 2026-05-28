#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, List, Sequence, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


Point = Tuple[float, float]


@dataclass
class Dataset:
    index: int
    depots: List[Point]
    targets: List[Point]
    weights: List[int]


@dataclass
class ResultRow:
    scenario: str
    test_name: str
    total_distance: float
    tours: List[List[int]]


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(
        description="Plot datasets and computed tours from method output CSV files."
    )
    p.add_argument("--datasets-file", type=Path, default=repo_root / "datasets.txt")
    p.add_argument("--output-root", type=Path, default=repo_root / "build" / "output")
    p.add_argument(
        "--methods",
        type=str,
        default="cluster_first,match_first,ca_ilp,ca_mis",
        help="Comma-separated methods to plot.",
    )
    p.add_argument(
        "--scenarios",
        type=str,
        default="scenario1,scenario2,scenario3",
        help="Comma-separated scenarios to plot.",
    )
    p.add_argument(
        "--datasets",
        type=str,
        default="all",
        help="Dataset ids, e.g. all or 1,2,10-20.",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Defaults to <output-root>/tour_plots.",
    )
    p.add_argument("--dpi", type=int, default=170)
    return p.parse_args()


def parse_point_list(raw: str) -> List[Point]:
    points: List[Point] = []
    for token in raw.split(";"):
        token = token.strip()
        if not token:
            continue
        x_raw, y_raw = token.split(",", 1)
        points.append((float(x_raw.strip()), float(y_raw.strip())))
    return points


def parse_int_list(raw: str) -> List[int]:
    return [int(token.strip()) for token in raw.split(",") if token.strip()]


def parse_datasets(path: Path) -> List[Dataset]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    datasets: List[Dataset] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith("Data set #"):
            i += 1
            continue

        index = int(line.split("#", 1)[1].strip())
        depots = parse_point_list(lines[i + 1].split(":", 1)[1])
        targets = parse_point_list(lines[i + 2].split(":", 1)[1])
        weights = parse_int_list(lines[i + 3].split("=", 1)[1])
        datasets.append(Dataset(index=index, depots=depots, targets=targets, weights=weights))
        i += 4
    return datasets


def parse_selection(raw: str, available: Iterable[int]) -> List[int]:
    if raw.strip().lower() == "all":
        return sorted(available)

    selected: List[int] = []
    for token in raw.split(","):
        token = token.strip()
        if not token:
            continue
        if "-" in token:
            start_raw, end_raw = token.split("-", 1)
            start, end = int(start_raw), int(end_raw)
            if start > end:
                start, end = end, start
            selected.extend(range(start, end + 1))
        else:
            selected.append(int(token))
    return sorted(set(selected))


def parse_tours(raw: str) -> List[List[int]]:
    tours: List[List[int]] = []
    for token in raw.split("|"):
        token = token.strip()
        if not token:
            continue
        parsed = ast.literal_eval(token)
        if not isinstance(parsed, list):
            raise ValueError(f"Invalid tour token: {token}")
        tours.append([int(v) for v in parsed])
    return tours


def read_result_rows(path: Path) -> List[ResultRow]:
    if not path.exists():
        return []

    rows: List[ResultRow] = []
    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                ResultRow(
                    scenario=row["scenario"],
                    test_name=row["test_name"],
                    total_distance=float(row["total_distance"]),
                    tours=parse_tours(row.get("tours", "")),
                )
            )
    return rows


def assign_targets_to_depots(dataset: Dataset) -> List[List[int]]:
    clusters: List[List[int]] = [[] for _ in dataset.depots]
    for target_idx, target in enumerate(dataset.targets):
        best_depot = min(
            range(len(dataset.depots)),
            key=lambda depot_idx: squared_distance(target, dataset.depots[depot_idx]),
        )
        clusters[best_depot].append(target_idx)
    return clusters


def squared_distance(a: Point, b: Point) -> float:
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return dx * dx + dy * dy


def cluster_first_mapper(dataset: Dataset, row: ResultRow) -> Callable[[int], Point]:
    match = re.search(r"cluster_(\d+)", row.test_name)
    if not match:
        raise ValueError(f"Cannot determine cluster from test_name={row.test_name!r}")

    depot_idx = int(match.group(1)) - 1
    clusters = assign_targets_to_depots(dataset)
    target_indices = clusters[depot_idx]

    def resolve(node_id: int) -> Point:
        if node_id == 0:
            return dataset.depots[depot_idx]
        local_idx = node_id - 1
        if 0 <= local_idx < len(target_indices):
            return dataset.targets[target_indices[local_idx]]
        return dataset.depots[depot_idx]

    return resolve


def match_first_mapper(dataset: Dataset, _row: ResultRow) -> Callable[[int], Point]:
    target_base = len(dataset.depots)

    def resolve(node_id: int) -> Point:
        if 0 <= node_id < len(dataset.depots):
            return dataset.depots[node_id]
        target_idx = node_id - target_base
        if 0 <= target_idx < len(dataset.targets):
            return dataset.targets[target_idx]
        raise ValueError(f"Unknown match_first node id {node_id}")

    return resolve


def auction_mapper(dataset: Dataset, _row: ResultRow) -> Callable[[int], Point]:
    depot_count = len(dataset.depots)

    def resolve(node_id: int) -> Point:
        if 0 <= node_id < depot_count:
            return dataset.depots[node_id]
        target_idx = node_id - depot_count
        if 0 <= target_idx < len(dataset.targets):
            return dataset.targets[target_idx]
        raise ValueError(f"Unknown auction node id {node_id}")

    return resolve


def draw_base(ax: plt.Axes, dataset: Dataset) -> None:
    if dataset.targets:
        xs, ys = zip(*dataset.targets)
        ax.scatter(xs, ys, s=42, c="#2d7dd2", edgecolors="white", linewidths=0.7, zorder=4)

    if dataset.depots:
        xs, ys = zip(*dataset.depots)
        ax.scatter(xs, ys, s=88, marker="s", c="#111111", edgecolors="white", linewidths=0.8, zorder=5)

    for idx, (x, y) in enumerate(dataset.depots):
        ax.annotate(str(idx), (x, y), xytext=(5, -10), textcoords="offset points", fontsize=9, weight="bold")


def draw_tours(
    ax: plt.Axes,
    dataset: Dataset,
    method: str,
    rows: Sequence[ResultRow],
) -> None:
    colors = plt.get_cmap("tab20").colors

    for row_idx, row in enumerate(rows):
        if method == "cluster_first":
            mapper = cluster_first_mapper(dataset, row)
        elif method == "match_first":
            mapper = match_first_mapper(dataset, row)
        elif method in {"ca_ilp", "ca_mis"}:
            mapper = auction_mapper(dataset, row)
        else:
            raise ValueError(f"Unsupported plotting method: {method}")

        for tour_idx, tour in enumerate(row.tours):
            if len(tour) < 2:
                continue

            path = tour + [tour[0]]
            points = [mapper(node_id) for node_id in path]
            xs, ys = zip(*points)
            color = colors[(row_idx * 5 + tour_idx) % len(colors)]
            ax.plot(xs, ys, color=color, linewidth=1.8, alpha=0.9, zorder=2)


def set_equal_axes(ax: plt.Axes, dataset: Dataset) -> None:
    points = dataset.depots + dataset.targets
    if not points:
        return

    xs, ys = zip(*points)
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    span = max(max_x - min_x, max_y - min_y, 1.0)
    pad = span * 0.12

    mid_x = (min_x + max_x) / 2.0
    mid_y = (min_y + max_y) / 2.0
    half = span / 2.0 + pad
    ax.set_xlim(mid_x - half, mid_x + half)
    ax.set_ylim(mid_y - half, mid_y + half)
    ax.set_aspect("equal", adjustable="box")


def plot_one(
    dataset: Dataset,
    method: str,
    scenario: str,
    rows: Sequence[ResultRow],
    output_path: Path,
    dpi: int,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 7))
    draw_tours(ax, dataset, method, rows)
    draw_base(ax, dataset)
    set_equal_axes(ax, dataset)

    total = sum(row.total_distance for row in rows)
    ax.set_title(f"{method} / {scenario} / dataset_{dataset.index:03d} / total={total}")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.grid(True, alpha=0.25)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(output_path, dpi=dpi)
    plt.close(fig)


def scenario_number(scenario: str) -> str:
    match = re.search(r"(\d+)$", scenario)
    if not match:
        raise ValueError(f"Scenario name must end in a number: {scenario}")
    return match.group(1)


def main() -> int:
    args = parse_args()
    datasets = parse_datasets(args.datasets_file)
    dataset_by_id = {dataset.index: dataset for dataset in datasets}
    selected = parse_selection(args.datasets, dataset_by_id.keys())
    methods = [m.strip() for m in args.methods.split(",") if m.strip()]
    scenarios = [s.strip() for s in args.scenarios.split(",") if s.strip()]
    output_root = args.output_root.resolve()
    output_dir = (args.output_dir or (output_root / "tour_plots")).resolve()

    written: List[Path] = []
    for method in methods:
        for scenario in scenarios:
            sc_num = scenario_number(scenario)
            for dataset_id in selected:
                dataset = dataset_by_id.get(dataset_id)
                if dataset is None:
                    continue

                result_csv = output_root / method / scenario / f"sc_{sc_num}_ds_{dataset_id}.csv"
                rows = read_result_rows(result_csv)
                if not rows:
                    continue

                out = output_dir / method / scenario / f"dataset_{dataset_id:03d}.png"
                plot_one(dataset, method, scenario, rows, out, args.dpi)
                written.append(out)

    if not written:
        print("No plots written. Check --output-root, --methods, --scenarios, and --datasets.")
        return 1

    print(f"Wrote {len(written)} plot(s) to: {output_dir}")
    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
