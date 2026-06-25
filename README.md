# MDCVRP: A Graph-Matching-Based Approach for the Multi-Depot Capacitated Vehicle Routing Problem

## Overview

This repo contains several heuristics for multi-depot constrained vehicle routing:

- `cluster_first`: assign targets to depots first, then solve per depot
- `match_first`: match globally first, then assign matched groups to depots
- `ca_ilp`: combinatorial auction with ILP set partitioning
- `ca_mis`: combinatorial auction with an MIS heuristic

## Setup

Requirements:

- CMake
- a C++17 compiler with the standard threading library (`std::thread`)
- [LEMON](https://lemon.cs.elte.hu/trac/lemon) headers and library (for `cluster_first` / `match_first`)
- OR-Tools for `ca_ilp`
- Python 3 (packages in `requirements.txt`)

LEMON has no system package, so build it from source into `$HOME/lemon`
(the location `CMakeLists.txt` expects) with the helper script:

```bash
scripts/install_deps.sh
```

This builds LEMON into `$HOME/lemon` and downloads a prebuilt OR-Tools into
`$HOME/or-tools` (both the locations `CMakeLists.txt` expects). On a platform
without a matching OR-Tools binary, point it at a release archive from
<https://github.com/google/or-tools/releases>:

```bash
ORTOOLS_URL=<archive-url> scripts/install_deps.sh
```

Configure and build:

```bash
cmake -S . -B build
cmake --build build -j
```

## Run Everything

To build, run every method/scenario, and generate the comparison CSVs and tour
plots in one step (set up the Python venv below first, for the plots):

```bash
scripts/run_everything.sh
```

Outputs land under `build/output/`. Methods whose dependencies are missing
(e.g. `ca_ilp` without OR-Tools) are skipped. The per-method commands below
are still available if you want to run them individually.

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

Set up a virtual environment and install the dependencies (once):

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

After the experiment outputs exist, run (from the repo root, with the venv active)
to compare outputs and plot tours:

```bash
python3 scripts/compare_costs.py
python3 scripts/compare_times.py
python3 scripts/plot_tours.py
```

For the higher-level summary plots, use:

```bash
python3 scripts/plot_base.py
python3 scripts/plot_capacity.py
python3 scripts/plot_scalability.py
```

These defaults use `datasets.txt`, `build/output`, and the available methods.

> [!NOTE]
> `compare_times.py` only compares the native C++ methods by default. `compare_costs.py` writes both dataset-wise plots and boxplots. `run_everything.sh` handles the filtering automatically, comparing only the methods that produced output.
