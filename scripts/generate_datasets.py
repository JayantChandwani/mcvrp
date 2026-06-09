#!/usr/bin/env python3
"""Generate datasets files in the canonical 4-line-per-block format.

Every dataset in a file shares ONE weight vector, so the capacity derived by
the solvers (scenario2 = 2*max_demand, scenario3 = 3*max_demand/2) is identical
across all datasets -- i.e. a single standardized capacity setting per file.

Usage:
  generate_datasets.py --out datasets.txt          --datasets 100 --depots 10 --targets 100
  generate_datasets.py --out datasets_scale.txt    --datasets 50  --depots 20 --targets 1000
"""
import argparse
import random

X_MAX = 200
Y_MAX = 300
W_MIN, W_MAX = 10, 100  # max weight fixes the derived capacity (kept at 100 across files)


def fmt_points(pts):
    return "".join(f"{x},{y};" for x, y in pts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--datasets", type=int, required=True)
    ap.add_argument("--depots", type=int, required=True)
    ap.add_argument("--targets", type=int, required=True)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)

    # One shared weight vector for the whole file -> standardized capacity.
    # Force at least one target at W_MAX so max_demand (and thus capacity) is pinned.
    weights = [rng.randint(W_MIN, W_MAX) for _ in range(args.targets)]
    weights[rng.randrange(args.targets)] = W_MAX
    weights_line = "Weights = " + ",".join(str(w) for w in weights)

    lines = []
    for idx in range(1, args.datasets + 1):
        depots = [(rng.randint(0, X_MAX), rng.randint(0, Y_MAX)) for _ in range(args.depots)]
        targets = [(rng.randint(0, X_MAX), rng.randint(0, Y_MAX)) for _ in range(args.targets)]
        lines.append(f"Data set #{idx}")
        lines.append("Vehicle locations :" + fmt_points(depots))
        lines.append("Target locations :" + fmt_points(targets))
        lines.append(weights_line)

    with open(args.out, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Wrote {args.out}: {args.datasets} datasets, {args.depots} depots, "
          f"{args.targets} targets, capacity max_demand={max(weights)} "
          f"(scenario2 C={2*max(weights)}, scenario3 C={3*max(weights)//2})")


if __name__ == "__main__":
    main()
