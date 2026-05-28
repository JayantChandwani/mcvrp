# MCVRP: Multi-Depot Constrained Vehicle Routing Problem Heuristics

## Overview

This repo contains several heuristics for multi-depot constrained vehicle routing:

- `cluster_first`: assign targets to depots first, then solve per depot
- `match_first`: match globally first, then assign matched groups to depots
- `ca_ilp`: combinatorial auction with ILP set partitioning
- `ca_mis`: combinatorial auction with an MIS heuristic

## Setup

Requirements:

- CMake
- a C++17 compiler
- OpenMP
- [LEMON](https://lemon.cs.elte.hu/trac/lemon) headers and library
- OR-Tools for `ca_ilp`
- Python 3 with `matplotlib`

The current `CMakeLists.txt` expects LEMON at:

```bash
$HOME/lemon/include
$HOME/lemon/lib/libemon.a
```

Configure and build:

```bash
cmake -S . -B build
cmake --build build -j
```

## Run Methods

Run from the build directories:

### `cluster_first`

```bash
cd build/cluster_first
./scenario1 ../../datasets.txt
./scenario2 ../../datasets.txt
./scenario3 ../../datasets.txt
```

### `match_first`

```bash
cd build/match_first
./scenario1 ../../datasets.txt
./scenario2 ../../datasets.txt
./scenario3 ../../datasets.txt
```

### `ca_ilp`

```bash
cd build/ca_ilp
./ca_ilp_main --input ../../datasets.txt --datasets 1-100 --scenario scenario1
./ca_ilp_main --input ../../datasets.txt --datasets 1-100 --scenario scenario2
./ca_ilp_main --input ../../datasets.txt --datasets 1-100 --scenario scenario3
```

### `ca_mis`

```bash
cd build/ca_mis
./ca_mis_main --input ../../datasets.txt --datasets 1-100 --scenario scenario1
./ca_mis_main --input ../../datasets.txt --datasets 1-100 --scenario scenario2
./ca_mis_main --input ../../datasets.txt --datasets 1-100 --scenario scenario3
```

## Outputs

Each method writes outputs under:

```text
build/output/<method>/<scenario>/
```

Important files:

- `sc_N_combined.csv`: one row per dataset with total distance and total runtime
- `sc_N_ds_<id>.csv`: detailed per-dataset tours
- `final_output.csv`: flattened per-route or per-cluster output

## Python Scripts

After the experiment outputs exist, run (from the repo root) to compare outputs and plot tours:

```bash
python3 scripts/compare_costs.py
python3 scripts/compare_times.py
python3 scripts/plot_tours.py
```

These defaults use `datasets.txt`, `build/output`, and all four methods.

> [!NOTE]
> The Python scripts currently skip missing method/scenario outputs rather than failing loudly. The normal workflow is to run all experiments first, then run the scripts. If you need different settings, refer to the script arguments directly.
