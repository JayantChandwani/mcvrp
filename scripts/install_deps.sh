#!/usr/bin/env bash
#
# Installs the libraries MCVRP needs:
#   LEMON    -> $HOME/lemon     (required by cluster_first / match_first)
#   OR-Tools -> $HOME/or-tools  (required by ca_ilp)
#
# Both paths match what CMakeLists.txt expects. ca_ilp / ca_mis parallelize
# with std::thread, so no OpenMP is needed.
#
# Usage: scripts/install_deps.sh
#   ORTOOLS_URL=<archive-url> scripts/install_deps.sh   # override OR-Tools download
set -euo pipefail

LEMON_VERSION="1.3.1"
LEMON_PREFIX="$HOME/lemon"
ORTOOLS_PREFIX="$HOME/or-tools"

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

# ---------------------------------------------------------------- LEMON
if [ -f "$LEMON_PREFIX/lib/libemon.a" ]; then
    echo "LEMON already present at $LEMON_PREFIX -- skipping."
else
    echo "Building LEMON $LEMON_VERSION into $LEMON_PREFIX ..."
    curl -L "http://lemon.cs.elte.hu/pub/sources/lemon-${LEMON_VERSION}.tar.gz" \
        -o "$workdir/lemon.tar.gz"
    tar -xzf "$workdir/lemon.tar.gz" -C "$workdir"

    cmake -S "$workdir/lemon-${LEMON_VERSION}" -B "$workdir/lemon-build" \
        -DCMAKE_INSTALL_PREFIX="$LEMON_PREFIX" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DLEMON_ENABLE_GLPK=NO -DLEMON_ENABLE_COIN=NO -DLEMON_ENABLE_ILOG=NO
    cmake --build "$workdir/lemon-build" -j
    cmake --install "$workdir/lemon-build"
    echo "LEMON installed at $LEMON_PREFIX"
fi

# -------------------------------------------------------------- OR-Tools
if [ -f "$ORTOOLS_PREFIX/lib/cmake/ortools/ortoolsConfig.cmake" ]; then
    echo "OR-Tools already present at $ORTOOLS_PREFIX -- skipping."
else
    url="${ORTOOLS_URL:-}"
    if [ -z "$url" ]; then
        echo "Resolving the latest OR-Tools C++ binary for this platform ..."
        # Pick the matching prebuilt C++ archive from the latest GitHub release.
        url="$(python3 - <<'PY'
import json, platform, sys, urllib.request

system, machine = platform.system(), platform.machine().lower()
if system == "Darwin":
    osname, arch = "macOS", ("arm64" if machine in ("arm64", "aarch64") else "x86_64")
elif system == "Linux":
    osname, arch = "linux", ("arm64" if machine in ("arm64", "aarch64") else "amd64")
else:
    sys.exit(1)

req = urllib.request.Request(
    "https://api.github.com/repos/google/or-tools/releases/latest",
    headers={"User-Agent": "mcvrp-install"},
)
assets = json.load(urllib.request.urlopen(req))["assets"]
names = [a["name"] for a in assets]

def match(n):
    if "_cpp_" not in n or not n.endswith(".tar.gz") or f"_{arch}_" not in n:
        return False
    return "macOS" in n if osname == "macOS" else ("ubuntu" in n or "debian" in n)

cands = sorted((n for n in names if match(n)), reverse=True)  # newest distro first
if not cands:
    sys.exit(1)
print(next(a["browser_download_url"] for a in assets if a["name"] == cands[0]))
PY
)" || {
            echo "Could not find a prebuilt OR-Tools C++ archive for this platform." >&2
            echo "Download one from https://github.com/google/or-tools/releases and re-run:" >&2
            echo "  ORTOOLS_URL=<archive-url> scripts/install_deps.sh" >&2
            exit 1
        }
    fi

    echo "Downloading OR-Tools from: $url"
    curl -L "$url" -o "$workdir/or-tools.tar.gz"
    rm -rf "$ORTOOLS_PREFIX"
    mkdir -p "$ORTOOLS_PREFIX"
    tar -xzf "$workdir/or-tools.tar.gz" -C "$ORTOOLS_PREFIX" --strip-components=1

    if [ ! -f "$ORTOOLS_PREFIX/lib/cmake/ortools/ortoolsConfig.cmake" ]; then
        echo "OR-Tools extracted but $ORTOOLS_PREFIX/lib/cmake/ortools/ortoolsConfig.cmake is missing." >&2
        exit 1
    fi
    echo "OR-Tools installed at $ORTOOLS_PREFIX"
fi

echo
echo "Done. Build with: cmake -S . -B build && cmake --build build -j"
